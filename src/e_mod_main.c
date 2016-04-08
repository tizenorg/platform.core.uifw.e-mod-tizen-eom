#define E_COMP_WL
#include "e.h"
#include "e_mod_main.h"
#include "eom-server-protocol.h"
#include <Eina.h>
#include <Ecore.h>
#include <Evas.h>
#include "Ecore_Drm.h"
#include <Ecore_Evas.h>
#include <Evas_Engine_Drm.h>
#include <tbm_bufmgr.h>
#include <tbm_surface.h>

#include <tdm.h>

#define NUM_MAIN_BUF 2
#define NUM_ATTR 3

typedef struct _E_Eom E_Eom, *E_EomPtr;
typedef struct _E_Eom_Out_Mode E_EomOutMode, *E_EomOutModePtr;
typedef struct _E_Eom_Data E_EomData, *E_EomDataPtr;

struct _E_Eom_Out_Mode
{
   int w;
   int h;
};

struct _E_Eom
{
   struct wl_global *global;
   struct wl_resource *resource;
   Eina_List *handlers;

   tdm_display *dpy;
   tbm_bufmgr bufmgr;
   int fd;

   /* eom state */
   enum wl_eom_mode eom_mode;
   enum wl_eom_attribute eom_attribute;
   enum wl_eom_attribute_state eom_attribute_state;
   enum wl_eom_status eom_status;

   /* external output data */
   char *ext_output_name;
   int is_external_init;
   int id;
   E_EomOutMode src_mode;
   E_Comp_Wl_Output *wl_output;

   /* internal output data */
   char *int_output_name;
   int is_internal_grab;
   E_EomOutMode dst_mode;
};

struct _E_Eom_Data
{
   tdm_layer *layer;
   tdm_output *output;
   tdm_pp *pp;

   tbm_surface_h dst_buffers[NUM_MAIN_BUF];
   int current_buffer;
   int pp_buffer;
};

static E_EomData g_eom_data;
E_EomPtr g_eom = NULL;
static E_Client_Hook *fullscreen_pre_hook = NULL;
E_API E_Module_Api e_modapi = { E_MODULE_API_VERSION, "EOM Module" };
static int eom_output_attributes[NUM_ATTR][NUM_ATTR] =
   {
      {1, 1, 1},
      {0, 1, 1},
      {0, 0, 0},
   };

/* handle external output */
static E_Comp_Wl_Output *_e_eom_e_comp_wl_output_get(const Eina_List *outputs, const char *id);
static int _e_eom_set_up_external_output(const char *output_name, int width, int height);
static tdm_output * _e_eom_hal_output_get(const char *id);
static tdm_layer * _e_eom_hal_layer_get(tdm_output *output, int width, int height);
static int _e_eom_create_output_buffers(E_EomDataPtr eom_data, int width, int height);
static enum wl_eom_type _e_eom_output_name_to_eom_type(const char *output_name);
/* handle internal output, pp */
static int _e_eom_root_internal_surface_get(const char *output_name, int width, int height);
static tbm_surface_h _e_eom_root_internal_tdm_surface_get(const char *name);
static int _e_eom_pp_src_to_dst( tbm_surface_h src_buffer);
/* tdm handlers */
static void _e_eom_pp_cb(tbm_surface_h surface, void *user_data);
static void _e_eom_output_commit_cb(tdm_output *output EINA_UNUSED, unsigned int sequence EINA_UNUSED,
                                    unsigned int tv_sec EINA_UNUSED, unsigned int tv_usec EINA_UNUSED,
                                    void *user_data);

static inline enum wl_eom_mode
_e_eom_get_eom_mode()
{
   return g_eom->eom_mode;
}

static inline void
_e_eom_set_eom_mode(enum wl_eom_mode mode)
{
   g_eom->eom_mode = mode;
}

static inline enum wl_eom_attribute_state
_e_eom_get_eom_attribute_state()
{
   return g_eom->eom_attribute_state;
}

static inline void
_e_eom_set_eom_attribute_state(enum wl_eom_attribute_state attribute_state)
{
   g_eom->eom_attribute_state = attribute_state;
}

static inline enum wl_eom_attribute
_e_eom_get_eom_attribute()
{
   return g_eom->eom_attribute;
}

static inline int
_e_eom_set_eom_attribute(enum wl_eom_attribute attribute)
{
   if (attribute == WL_EOM_ATTRIBUTE_NONE || g_eom->eom_attribute == WL_EOM_ATTRIBUTE_NONE)
     {
        g_eom->eom_attribute = attribute;
        return 1;
     }

   if (eom_output_attributes[g_eom->eom_attribute - 1][attribute - 1] == 1)
     {
        g_eom->eom_attribute = attribute;
        return 1;
     }

   return 0;
}

static inline enum wl_eom_status
_e_eom_get_eom_status()
{
   return g_eom->eom_status;
}

static inline void
_e_eom_set_eom_status(enum wl_eom_status status)
{
   g_eom->eom_status = status;
}

static void
_e_eom_pp_cb(tbm_surface_h surface, void *user_data)
{
   E_EomDataPtr eom_data = NULL;
   tdm_error tdm_err = TDM_ERROR_NONE;

   if (!user_data)
     {
        EOM_DBG("ERROR: PP EVENT: user data is NULL\n");
        return;
     }

   eom_data = (E_EomDataPtr)user_data;

   tdm_buffer_remove_release_handler(eom_data->dst_buffers[eom_data->pp_buffer],
                                        _e_eom_pp_cb, eom_data);

   tbm_surface_h src_buffer;
   src_buffer = _e_eom_root_internal_tdm_surface_get(g_eom->int_output_name);
   if (!src_buffer )
     {
        EOM_DBG("ERROR: PP EVENT: get root tdm surfcae\n");
        return;
     }

   g_eom_data.pp_buffer = !g_eom_data.current_buffer;

   tdm_err = tdm_buffer_add_release_handler(g_eom_data.dst_buffers[g_eom_data.pp_buffer],
                                            _e_eom_pp_cb, &g_eom_data);
   if (tdm_err  != TDM_ERROR_NONE)
     {
        EOM_DBG ("ERROR: PP EVENT: set pp hadler:%d\n", tdm_err );
        return;
     }

   tdm_err = tdm_pp_attach(eom_data->pp, src_buffer, g_eom_data.dst_buffers[g_eom_data.pp_buffer]);
   if (tdm_err != TDM_ERROR_NONE)
     {
        printf ("ERROR: pp attach:%d\n", tdm_err);
        return;
     }

   tdm_err  = tdm_pp_commit(g_eom_data.pp);
   if (tdm_err  != TDM_ERROR_NONE)
     {
        EOM_DBG ("ERROR: PP EVENT: pp commit:%d\n", tdm_err );
        return;
     }
}

static void
_e_eom_output_commit_cb(tdm_output *output EINA_UNUSED, unsigned int sequence EINA_UNUSED,
                           unsigned int tv_sec EINA_UNUSED, unsigned int tv_usec EINA_UNUSED,
                           void *user_data)
{
   E_EomDataPtr eom_data;
   tdm_error err = TDM_ERROR_NONE;

   if (!user_data)
     {
        EOM_ERR("ERROR: EVENT: user_data is NULL\n");
        return;
     }

   eom_data = (E_EomDataPtr)user_data;

   if (eom_data->current_buffer == 1)
     {
        eom_data->current_buffer = 0;

        err = tdm_layer_set_buffer(eom_data->layer,
                                   eom_data->dst_buffers[!eom_data->pp_buffer]);
       if (err != TDM_ERROR_NONE)
         {
            EOM_ERR("ERROR: EVENT: set buffer 0\n");
            return;
         }
     }
   else
     {
        eom_data->current_buffer = 1;

        err = tdm_layer_set_buffer(eom_data->layer,
                                   eom_data->dst_buffers[!eom_data->pp_buffer]);
        if (err != TDM_ERROR_NONE)
          {
             EOM_ERR("ERROR: EVENT: set buffer 1\n");
             return;
          }
     }

   err = tdm_output_commit(eom_data->output, 0, _e_eom_output_commit_cb, eom_data);
   if (err != TDM_ERROR_NONE)
     {
        EOM_ERR("ERROR: EVENT: commit\n");
        return;
     }
}

static E_Comp_Wl_Output *
_e_eom_e_comp_wl_output_get(const Eina_List *outputs, const char *id)
{
   const Eina_List *l;
   E_Comp_Wl_Output *output = NULL, *o;
   int loc = 0;

   EINA_LIST_FOREACH(outputs, l, o)
     {
        char *temp_id = NULL;
        temp_id = strchr(o->id, '/');

        EOM_DBG("o->id=%s", o->id);

        if (temp_id == NULL)
          {
             if (strcmp(o->id, id) == 0)
               output = o;
          }
        else
          {
             loc = temp_id - o->id;

             if (strncmp(o->id, id, loc) == 0)
               output = o;
          }
     }

   if (!output)
     return NULL;
   return output;
}

static int
_e_eom_set_up_external_output(const char *output_name, int width, int height)
{
   tdm_output *hal_output;
   tdm_layer *hal_layer;
   tdm_info_layer layer_info;
   tdm_error tdm_err = TDM_ERROR_NONE;
   E_EomDataPtr eom_data = &g_eom_data;
   int ret = 0;


   hal_output = _e_eom_hal_output_get(output_name);
   if (!hal_output)
     {
        EOM_ERR("ERROR: get hal output for, (%s)\n", output_name);
        goto err;
     }

   hal_layer = _e_eom_hal_layer_get(hal_output, width, height);
   if (!hal_layer)
     {
        EOM_ERR("ERROR: get hal layer\n");
        goto err;
     }

   ret = _e_eom_create_output_buffers(eom_data, width, height);
   if (!ret )
     {
        EOM_ERR("ERROR: create buffers \n");
        goto err;
     }

   tdm_err = tdm_layer_get_info(hal_layer, &layer_info);
   if (tdm_err != TDM_ERROR_NONE)
     {
        EOM_ERR ("ERROR: get layer info: %d", tdm_err);
        goto err;
     }

   EOM_DBG("LAYER INFO: %dx%d, pos (x:%d, y:%d, w:%d, h:%d,  dpos (x:%d, y:%d, w:%d, h:%d))",
           layer_info.src_config.size.h,  layer_info.src_config.size.v,
           layer_info.src_config.pos.x, layer_info.src_config.pos.y,
           layer_info.src_config.pos.w, layer_info.src_config.pos.h,
           layer_info.dst_pos.x, layer_info.dst_pos.y,
           layer_info.dst_pos.w, layer_info.dst_pos.h);

   g_eom->dst_mode.w = width;
   g_eom->dst_mode.h = height;
   /* TODO: free that memory */
   g_eom->ext_output_name = strdup(output_name);

   eom_data->layer = hal_layer;
   eom_data->output = hal_output;
   eom_data->current_buffer = 0;

   tdm_err = tdm_layer_set_buffer(hal_layer, eom_data->dst_buffers[eom_data->current_buffer]);
   if (tdm_err != TDM_ERROR_NONE)
     {
        EOM_ERR("ERROR: set buffer on layer:%d\n", tdm_err);
        goto err;
     }

   /* TODO: it is commented because we do not have HDMI events
    * temprary commit moved to pp section
    */
   tdm_err = tdm_output_commit(hal_output, 0, _e_eom_output_commit_cb, eom_data);
   if (tdm_err != TDM_ERROR_NONE)
     {
        EOM_ERR("ERROR: commit crtc:%d\n", tdm_err);
        goto err;
     }


   return 1;

err:
/*
 * TODO: add deinitialization
 */
   return 0;
}

static void
_e_eom_deinit_external_output()
{
   tdm_error err = TDM_ERROR_NONE;
   int i = 0;

   if (g_eom_data.layer)
     {
        err = tdm_layer_unset_buffer(g_eom_data.layer);
        if (err != TDM_ERROR_NONE)
          EOM_DBG("EXT OUTPUT DEINIT: fail unset buffer:%d\n", err);
        else
          EOM_DBG("EXT OUTPUT DEINIT: ok unset buffer:%d\n", err);

        err = tdm_output_commit(g_eom_data.output, 0, NULL, &g_eom_data);
        if (err != TDM_ERROR_NONE)
          EOM_DBG ("EXT OUTPUT DEINIT: fail commit:%d\n", err);
        else
          EOM_DBG("EXT OUTPUT DEINIT: ok commit:%d\n", err);

        for (i = 0; i < NUM_MAIN_BUF; i++)
          {
             tdm_buffer_remove_release_handler(g_eom_data.dst_buffers[i],
                                               _e_eom_pp_cb, &g_eom_data);
             if (g_eom_data.dst_buffers[i])
               tbm_surface_destroy(g_eom_data.dst_buffers[i]);
          }
    }

   if (g_eom->wl_output)
      g_eom->wl_output = NULL;
}

static tdm_output *
_e_eom_hal_output_get(const char *id)
{
   Ecore_Drm_Device *dev;
   Ecore_Drm_Output *drm_output = NULL, *o;
   tdm_output *output;
   const tdm_output_mode *modes;
   const tdm_output_mode *big_mode;
   tdm_error err = TDM_ERROR_NONE;
   const Eina_List *l, *ll;
   int crtc_id = 0;
   int count = 0;

   /*
    * TODO: Temporary take into account only HDMI
    */
   EINA_LIST_FOREACH(ecore_drm_devices_get(), l, dev)
     {
        EINA_LIST_FOREACH(dev->external_outputs, ll, o)
          {
             if ((ecore_drm_output_name_get(o)) && (strcmp(id, ecore_drm_output_name_get(o)) == 0))
               drm_output = o;
          }
     }

   if (!drm_output)
     {
        EOM_ERR("ERROR: drm output was not found\n");
        return NULL;
     }

   crtc_id = ecore_drm_output_crtc_id_get(drm_output);
   if (crtc_id == 0)
    {
       EOM_ERR("ERROR: crtc is 0\n");
       return NULL;
    }

   output = tdm_display_get_output(g_eom->dpy, crtc_id, NULL);
   if (!output)
     {
        EOM_ERR("ERROR: there is no HAL output for:%d\n", crtc_id);
        return NULL;
     }

   int min_w, min_h, max_w, max_h, preferred_align;
   err = tdm_output_get_available_size(output, &min_w, &min_h, &max_w, &max_h, &preferred_align);
   if (err != TDM_ERROR_NONE)
     {
        EOM_ERR("ERROR: Gent get geometry for hal output");
        return NULL;
     }

   EOM_DBG("HAL size min:%dx%d  max:%dx%d  alighn:%d\n",
         min_w, min_h, max_w, max_h, preferred_align);

   /*
    * Force TDM to make setCrtc onto new buffer
    */
   err = tdm_output_get_available_modes(output, &modes, &count);
   if (err != TDM_ERROR_NONE)
     {
        EOM_ERR("Get availvable modes filed\n");
        return NULL;
     }

   big_mode = &modes[0];

   int i = 0;
   for (i = 0; i < count; i++)
     {
        if ((modes[i].vdisplay + modes[i].hdisplay) >= (big_mode->vdisplay + big_mode->hdisplay))
          big_mode = &modes[i];
     }

   /*TODO: fix it*/
   if (!big_mode)
     {
        EOM_ERR("no Big mode\n");
        return NULL;
     }

   EOM_DBG("BIG_MODE: %dx%d\n", big_mode->hdisplay, big_mode->vdisplay);

   err = tdm_output_set_mode(output, big_mode);
   if (err != TDM_ERROR_NONE)
     {
        EOM_ERR("set Mode failed\n");
        return NULL;
     }

   EOM_DBG("Created output: %p\n", output);
   return output;
}

static tdm_layer *
_e_eom_hal_layer_get(tdm_output *output, int width, int height)
{
   int i = 0;
   int count = 0;
   tdm_layer *layer = NULL;
   tdm_error err = TDM_ERROR_NONE;
   tdm_layer_capability capa;
   tdm_info_layer layer_info;


   err = tdm_output_get_layer_count(output, &count);
   if (err != TDM_ERROR_NONE)
     {
        EOM_DBG ("tdm_output_get_layer_count fail(%d)\n", err);
        return NULL;
     }

   for (i = 0; i < count; i++)
     {
        layer = (tdm_layer *)tdm_output_get_layer(output, i, &err);
        if (err != TDM_ERROR_NONE)
          {
             EOM_DBG ("tdm_output_get_layer fail(%d)\n", err);
             return NULL;
          }

        err = tdm_layer_get_capabilities(layer, &capa);
        if (err != TDM_ERROR_NONE)
          {
             EOM_DBG ("tdm_layer_get_capabilities fail(%d)\n", err);
             return NULL;
          }

        if (capa & TDM_LAYER_CAPABILITY_PRIMARY)
          {
             EOM_DBG("TDM_LAYER_CAPABILITY_PRIMARY layer found : %d\n", i);
             break;
          }
     }

   memset(&layer_info, 0x0, sizeof(tdm_info_layer));
   layer_info.src_config.size.h = width;
   layer_info.src_config.size.v = height;
   layer_info.src_config.pos.x = 0;
   layer_info.src_config.pos.y = 0;
   layer_info.src_config.pos.w = width;
   layer_info.src_config.pos.h = height;
   layer_info.src_config.format = TBM_FORMAT_ARGB8888;
   layer_info.dst_pos.x = 0;
   layer_info.dst_pos.y = 0;
   layer_info.dst_pos.w = width;
   layer_info.dst_pos.h = height;
   layer_info.transform = TDM_TRANSFORM_NORMAL;

   err = tdm_layer_set_info(layer, &layer_info);
   if (err != TDM_ERROR_NONE)
     {
        EOM_DBG ("tdm_layer_set_info fail(%d)\n", err);
        return NULL;
     }

   return layer;
}

static int
_e_eom_create_output_buffers(E_EomDataPtr eom_data, int width, int height)
{
   tbm_surface_h buffer;
   tbm_surface_info_s buffer_info;

   /*
    * TODO: Add support of other formats
    */
   buffer = tbm_surface_internal_create_with_flags(width, height, TBM_FORMAT_ARGB8888, TBM_BO_SCANOUT);
   if (!buffer)
     {
        EOM_DBG("can not create dst_buffer\n");
        goto err;
     }

   /*
    * TODO: temp code for testing, actual convert will be in _e_eom_put_src_to_dst()
    */

   memset(&buffer_info, 0x0, sizeof(tbm_surface_info_s));
   if (tbm_surface_map(buffer,
                       TBM_SURF_OPTION_READ | TBM_SURF_OPTION_WRITE,
                       &buffer_info) != TBM_SURFACE_ERROR_NONE)
     {
        EOM_DBG("can not mmap buffer\n");
        goto err;
     }

   memset(buffer_info.planes[0].ptr, 0xFF, buffer_info.planes[0].size);
   tbm_surface_unmap(buffer);

   eom_data->dst_buffers[0] = buffer;

   /*
    * TODO: Add support of other formats
    */
   buffer = tbm_surface_internal_create_with_flags(width, height, TBM_FORMAT_ARGB8888, TBM_BO_SCANOUT);
   if (!buffer)
     {
        EOM_DBG("can not create dst_buffer\n");
        goto err;
     }

   /*
    * TODO: temp code for testing, actual convert will be in _e_eom_put_src_to_dst()
    */
   memset(&buffer_info, 0x00, sizeof(tbm_surface_info_s));
   if (tbm_surface_map(buffer,
                       TBM_SURF_OPTION_READ | TBM_SURF_OPTION_WRITE,
                       &buffer_info) != TBM_SURFACE_ERROR_NONE)
     {
        EOM_DBG("can not mmap buffer\n");
        goto err;
     }

   memset(buffer_info.planes[0].ptr, 0xFF, buffer_info.planes[0].size);
   tbm_surface_unmap(buffer);

   eom_data->dst_buffers[1] = buffer;

   return 1;

err:

/*
 * Add deinitialization
 */
   return 0;
}

static enum wl_eom_type
_e_eom_output_name_to_eom_type(const char *output_name)
{
   enum wl_eom_type eom_type;

   /* TODO: Add other external outputs */
   if (strcmp(output_name, "HDMI-A-0") == 0)
     eom_type = WL_EOM_TYPE_HDMIA;
   else
     eom_type = WL_EOM_TYPE_NONE;

   return eom_type;
}

static int
_e_eom_root_internal_surface_get(const char *output_name, int width, int height)
{
   tbm_surface_h src_buffer;
   tbm_surface_info_s src_buffer_info;
   int ret = 0;

   src_buffer = _e_eom_root_internal_tdm_surface_get(output_name);
   if (!src_buffer )
     {
        EOM_ERR("ERROR: get root tdm surfcae\n");
        return 0;
     }

   tbm_surface_get_info(src_buffer, &src_buffer_info );

   EOM_DBG("FRAMEBUFFER TDM: %dx%d   bpp:%d   size:%d",
           src_buffer_info.width,
           src_buffer_info.height,
           src_buffer_info.bpp,
           src_buffer_info.size);

   g_eom->src_mode.w = width;
   g_eom->src_mode.h = height;
   /* TODO: free that memory */
   g_eom->int_output_name = strdup(output_name);

   EOM_DBG("INT SURFACE: 1\n");

   ret = _e_eom_pp_src_to_dst(src_buffer);
   if (!ret )
     {
        EOM_ERR("ERROR: init pp\n");
        return 0;
     }

   EOM_DBG("INT SURFACE: 2\n");

   g_eom->is_internal_grab = 1;

   return 1;
}

static tbm_surface_h
_e_eom_root_internal_tdm_surface_get(const char *name)
{
   Ecore_Drm_Output *primary_output = NULL;
   Ecore_Drm_Device *dev;
   const Eina_List *l;
   Ecore_Drm_Fb *fb;

   EINA_LIST_FOREACH(ecore_drm_devices_get(), l, dev)
     {
        primary_output = ecore_drm_device_output_name_find(dev, name);
     }

   if (!primary_output)
     {
         EOM_ERR("ERROR: get primary output\n");
         return NULL;
     }

   /* I think it is more convenient than one upon, but E took first
    * output as primary and it can be not internal output
    *
   primary_output = ecore_drm_output_primary_get();
   if (!primary_output)
     {
       EOM_ERR("ERROR: get primary output\n");
       return NULL;
     }
   */

   fb = ecore_drm_display_output_primary_layer_fb_get(primary_output);
   if (!primary_output)
     {
        EOM_ERR("ERROR: get primary frambuffer\n");
        return NULL;
     }

   //EOM_DBG("FRAMEBUFFER ECORE_DRM: is_client:%d mode%dx%d\n", fb->from_client, fb->w, fb->h);

   return (tbm_surface_h)fb->hal_buffer;
}

static int
_e_eom_pp_src_to_dst( tbm_surface_h src_buffer)
{
   tdm_pp *pp;
   tdm_info_pp pp_info;
   tdm_error err = TDM_ERROR_NONE;

   EOM_DBG("PP: 1\n");

   pp = tdm_display_create_pp(g_eom->dpy, &err);
   if (err != TDM_ERROR_NONE)
     {
        EOM_ERR("ERROR: create pp:%d\n", err);
        return 0;
     }

   EOM_DBG("PP: 2\n");

   g_eom_data.pp = pp;

   pp_info.src_config.size.h = g_eom->src_mode.w; /*1440*/
   pp_info.src_config.size.v = g_eom->src_mode.h; /*2560*/
   pp_info.src_config.pos.x = 0;
   pp_info.src_config.pos.y = 0;
   pp_info.src_config.pos.w = g_eom->src_mode.w; /*1440*/
   pp_info.src_config.pos.h = g_eom->src_mode.h; /*2560*/
   pp_info.src_config.format = TBM_FORMAT_ARGB8888;
   pp_info.dst_config.size.h = g_eom->dst_mode.w; /*1960*/
   pp_info.dst_config.size.v = g_eom->dst_mode.h; /*1080*/
   pp_info.dst_config.pos.x = 0;
   pp_info.dst_config.pos.y = 0;
   pp_info.dst_config.pos.w = g_eom->dst_mode.w; /*1960*/
   pp_info.dst_config.pos.h = g_eom->dst_mode.h; /*1080*/
   pp_info.dst_config.format = TBM_FORMAT_ARGB8888;
   pp_info.transform = TDM_TRANSFORM_NORMAL;/*TDM_TRANSFORM_NORMAL*/
   pp_info.sync = 0;
   pp_info.flags = 0;

   EOM_DBG("PP: 3\n");

   err = tdm_pp_set_info(pp, &pp_info);
   if (err != TDM_ERROR_NONE)
     {
        EOM_ERR("ERROR: set pp info:%d\n", err);
        return 0;
     }

   EOM_DBG("PP: 4\n");

   g_eom_data.pp_buffer = !g_eom_data.current_buffer;
   EOM_DBG("PP: curr:%d  pp:%d\n", g_eom_data.current_buffer, g_eom_data.pp_buffer);

   err = tdm_buffer_add_release_handler(g_eom_data.dst_buffers[g_eom_data.pp_buffer],
                                      _e_eom_pp_cb, &g_eom_data);
   if (err != TDM_ERROR_NONE)
     {
        EOM_ERR ("ERROR: set pp hadler:%d\n", err);
        return 0;
     }

   EOM_DBG("PP: 5\n");

   err = tdm_pp_attach(pp, src_buffer, g_eom_data.dst_buffers[g_eom_data.pp_buffer]);
   if (err != TDM_ERROR_NONE)
     {
        EOM_ERR("ERROR: pp attach:%d\n", err);
        return 0;
     }


   EOM_DBG("PP: 6\n");

   err = tdm_pp_commit(g_eom_data.pp);
   if (err != TDM_ERROR_NONE)
     {
         EOM_ERR("ERROR: pp commit:%d\n", err);
        return 0;
     }

   EOM_DBG("PP: OK\n");

   return 1;
}

static int flag = 0;

static Eina_Bool
_e_eom_ecore_drm_output_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Drm_Event_Output *e;
   E_EomPtr eom = data;
   E_Comp_Wl_Output *wl_output;
   const Eina_List *l;
   struct wl_resource *output_resource;
   enum wl_eom_type eom_type = WL_EOM_TYPE_NONE;
   char buff[PATH_MAX];
   int ret = 0;

   if (!(e = event)) goto end;

   if (!e->plug) goto end;

   EOM_DBG("id:%d (x,y,w,h):(%d,%d,%d,%d) (w_mm,h_mm):(%d,%d) refresh:%d subpixel_order:%d transform:%d make:%s model:%s name:%s plug:%d\n",
         e->id, e->x, e->y, e->w, e->h, e->phys_width, e->phys_height, e->refresh, e->subpixel_order, e->transform, e->make, e->model, e->name, e->plug);

   snprintf(buff, sizeof(buff), "%s", e->name);

   if (strcmp(e->name, "HDMI-A-0") == 0)
     {
       if (e->plug == 1)
         {
            /* Get e_comp_wl_output */
            wl_output = _e_eom_e_comp_wl_output_get(e_comp_wl->outputs, buff);
            if (!wl_output)
              {
                 EOM_ERR("ERROR: there is no wl_output:(%s)\n", buff);
                 goto end;
              }

            /* Initialize external output */
            ret = _e_eom_set_up_external_output(buff, e->w, e->h);
            if (!ret)
              {
                 EOM_ERR("ERROR: initialize external output\n");
                 goto end;
              }

            g_eom->is_external_init = 1;
            g_eom->id = e->id;

            _e_eom_set_eom_attribute_state(WL_EOM_ATTRIBUTE_STATE_ACTIVE);
            _e_eom_set_eom_status(WL_EOM_STATUS_CONNECTION);
            _e_eom_set_eom_attribute(WL_EOM_ATTRIBUTE_NONE);
            _e_eom_set_eom_mode(WL_EOM_MODE_MIRROR);
         }
       else
         {
            g_eom->is_external_init = 0;
            g_eom->is_internal_grab = 0;
            g_eom->id = -1;

            _e_eom_set_eom_attribute_state(WL_EOM_ATTRIBUTE_STATE_INACTIVE);
            _e_eom_set_eom_status(WL_EOM_STATUS_DISCONNECTION);
            _e_eom_set_eom_attribute(WL_EOM_ATTRIBUTE_NONE);
            _e_eom_set_eom_mode(WL_EOM_MODE_NONE);

            _e_eom_deinit_external_output();
         }

        eom_type = _e_eom_output_name_to_eom_type(buff);
        if (eom_type == WL_EOM_TYPE_NONE)
          {
             EOM_ERR("ERROR: eom_type is NONE\n");
             goto end;
          }

        g_eom->wl_output = wl_output;

        /*
        EINA_LIST_FOREACH(wl_output->resources, l, output_resource)
          {

             EOM_DBG("e->plug:%d\n", e->plug);

             wl_eom_send_output_type(eom->resource,
                                     output_resource,
                                     eom_type,
                                     _e_eom_get_eom_status());

             wl_eom_send_output_attribute(eom->resource,
                                          output_resource,
                                          _e_eom_get_eom_attribute(),
                                          _e_eom_get_eom_attribute_state(),
                                          WL_EOM_ERROR_NONE);

             wl_eom_send_output_mode(eom->resource,
                                     output_resource,
                                     _e_eom_get_eom_mode());
          }
        */
     }
   else if (strcmp(e->name, "DSI-0") == 0 && g_eom->is_external_init && flag == 2)
     {
        ret = _e_eom_root_internal_surface_get(buff, e->w, e->h);
        if (!ret)
          {
             EOM_ERR("ERROR: get root surfcae\n");
             goto end;
          }
     }

   ++flag;

end:
   return ECORE_CALLBACK_PASS_ON;
}


void
_e_eom_set_output(Ecore_Drm_Output * drm_output, tbm_surface_h surface)
{
   /* TODO: chack save and commit*/
}

static Ecore_Drm_Output *
_e_eom_get_drm_output_for_client(E_Client *ec)
{
   Ecore_Drm_Output *drm_output;
   Ecore_Drm_Device *dev;
   const Eina_List *l;

   /* TODO: get real output, now we just return HDMI */
   EINA_LIST_FOREACH(ecore_drm_devices_get(), l, dev)
     {
        drm_output = ecore_drm_device_output_name_find(dev, "HDMI-A-0");
        if (drm_output)
          return drm_output;
     }

   return NULL;
}


static tbm_surface_h
_e_eom_get_tbm_surface_for_client(E_Client *ec)
{
   /*
   E_Pixmap *pixmap = ec->pixmap;
   E_Comp_Wl_Buffer *buffer = e_pixmap_resource_get(pixmap);
   tbm_surface_h tsurface = NULL;
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *) e_comp->wl_comp_data;

   EINA_SAFETY_ON_NULL_RETURN_VAL(buffer != NULL, NULL);

   tsurface = wayland_tbm_server_get_surface(wl_comp_data->tbm.server, buffer->resource);

   return tsurface;
   */
}

static void
_e_eom_canvas_render_post(void *data EINA_UNUSED, Evas *e EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Ecore_Drm_Output * drm_output;
   tbm_surface_h surface;

   E_Client *ec = data;
   EINA_SAFETY_ON_NULL_RETURN(ec != NULL);
   EINA_SAFETY_ON_NULL_RETURN(ec->frame != NULL);

   drm_output = _e_eom_get_drm_output_for_client(ec);
   EINA_SAFETY_ON_NULL_RETURN(drm_output != NULL);

   surface = _e_eom_get_tbm_surface_for_client(ec);

   _e_eom_set_output(drm_output, surface);
}

static void
_e_eom_fullscreen_pre_cb_hook(void *data, E_Client *ec)
{
   /*
   const Eina_List *l;
   E_Comp_Wl_Output *ext_output = NULL;
   int loc = 0;

   Ecore_Drm_Output * drm_output;
   tbm_surface_h surface;

   EINA_SAFETY_ON_NULL_RETURN(ec != NULL);
   EINA_SAFETY_ON_NULL_RETURN(ec->frame != NULL);


   if (g_eom->is_external_init == 0 &&
      (ext_output = _e_eom_e_comp_wl_output_get(e_comp_wl->outputs, g_eom->ext_output_name)) == NULL)
     {
        EINA_LIST_FOREACH(ext_output->clients, l, o)
          {
             ;
          }
     }

   drm_output = _e_eom_get_drm_output_for_client(ec);
   EINA_SAFETY_ON_NULL_RETURN(drm_output != NULL);

   surface = _e_eom_get_tbm_surface_for_client(ec);

   _e_eom_set_output(drm_output, surface);

   evas_event_callback_add(ec->frame, EVAS_CALLBACK_RENDER_POST, _e_eom_canvas_render_post, ec);
   */
}


static Eina_Bool
_e_eom_ecore_drm_activate_cb(void *data, int type EINA_UNUSED, void *event)
{
   Ecore_Drm_Event_Activate *e = NULL;
   /* E_EomPtr eom = NULL; */

   EOM_DBG("_e_eom_ecore_drm_activate_cb called\n");

   if ((!event) || (!data)) goto end;
   e = event;
   /* eom = data; */

   EOM_DBG("e->active:%d\n", e->active);

   if (e->active)
     {
        /* TODO: do something */
     }
   else
     {
        /* TODO: do something */
     }

end:
   return ECORE_CALLBACK_PASS_ON;
}


/* wl_eom_set_keygrab request handler */
static void
_e_eom_wl_request_set_attribute_cb(struct wl_client *client, struct wl_resource *resource, uint32_t output_id, uint32_t attribute)
{
   int ret = 0;
	/*
   (void) client;
   (void) attribute;
   */

   EOM_DBG("attribute:%d +++ output:%d\n", attribute, output_id);

   /*
    * TODO: check output_id
    */
   ret = _e_eom_set_eom_attribute(attribute);
   if (ret == 0)
     {
	    EOM_DBG("set attribute FAILED\n");

	    wl_eom_send_output_attribute(resource,
                                     g_eom->id,
                                     _e_eom_get_eom_attribute(),
                                     _e_eom_get_eom_attribute_state(),
                                     WL_EOM_ERROR_OUTPUT_OCCUPIED);
     }
   else
     {
	    EOM_DBG("set attribute OK\n");

	    wl_eom_send_output_attribute(resource,
                                     g_eom->id,
                                     _e_eom_get_eom_attribute(),
                                     _e_eom_get_eom_attribute_state(),
                                     WL_EOM_ERROR_NONE);
     }

}

static const struct wl_eom_interface _e_eom_wl_implementation =
{
   _e_eom_wl_request_set_attribute_cb
};

/* wl_eom global object destroy function */
static void
_e_eom_wl_resource_destory_cb(struct wl_resource *resource)
{
   /* TODO : destroy resources if exist */
}

/* wl_eom global object bind function */
static void
_e_eom_wl_bind_cb(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   enum wl_eom_type eom_type = WL_EOM_TYPE_NONE;
   struct wl_resource *output_resource;
   E_Comp_Wl_Output *wl_output = NULL;
   struct wl_resource *resource;
   E_EomPtr eom = data;
   const Eina_List *l;

   resource = wl_resource_create(client,
                         &wl_eom_interface,
                         MIN(version, 1),
                         id);
   if (!resource)
     {
        EOM_ERR("error. resource is null. (version :%d, id:%d)\n", version, id);
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(resource,
                          &_e_eom_wl_implementation,
                          eom,
                          _e_eom_wl_resource_destory_cb);

   eom->resource = resource;

   wl_output = _e_eom_e_comp_wl_output_get(e_comp_wl->outputs, g_eom->ext_output_name);
   if (!wl_output)
     {
        EOM_DBG("failed to get wl_output\n");
        return;
     }

   eom_type = _e_eom_output_name_to_eom_type(g_eom->ext_output_name);
   if (eom_type == WL_EOM_TYPE_NONE)
     {
        EOM_DBG("create wl_eom global resource.\n");
        return;
     }

   wl_eom_send_output_type(eom->resource,
                           eom->id,
                           eom_type,
                           _e_eom_get_eom_status());

   wl_eom_send_output_attribute(eom->resource,
                                eom->id,
                                _e_eom_get_eom_attribute(),
                                _e_eom_get_eom_attribute_state(),
                                WL_EOM_ERROR_NONE);

   wl_eom_send_output_mode(eom->resource,
                           eom->id,
                           _e_eom_get_eom_mode());

   EOM_DBG("create wl_eom global resource.\n");
}

static void
_e_eom_deinit()
{
   Ecore_Event_Handler *h;

   if (!g_eom) return;

   if (g_eom->handlers)
     {
        EINA_LIST_FREE(g_eom->handlers, h)
        ecore_event_handler_del(h);
     }

   if (fullscreen_pre_hook)
     {
        e_client_hook_del(fullscreen_pre_hook);
        fullscreen_pre_hook = NULL;
     }

   if (g_eom->global) wl_global_destroy(g_eom->global);

   E_FREE(g_eom);
}

int
_e_eom_init_internal()
{
   tdm_error err = TDM_ERROR_NONE;

   g_eom->dpy = tdm_display_init(&err);
   if (err != TDM_ERROR_NONE)
     {
        EOM_DBG("failed initialize TDM\n");
        goto err;
     }

   err = tdm_display_get_fd(g_eom->dpy, &g_eom->fd);
   if (err != TDM_ERROR_NONE)
     {
        EOM_DBG("failed get FD\n");
        goto err;
     }

   g_eom->bufmgr = tbm_bufmgr_init(g_eom->fd);
   if (!g_eom->bufmgr)
     {
        EOM_DBG("failed initialize buffer manager\n");
        goto err;
     }

   return 1;
err:
   return 0;
}

static Eina_Bool
_e_eom_init()
{
   int ret = 0;

   EINA_SAFETY_ON_NULL_GOTO(e_comp_wl, err);

   g_eom = E_NEW(E_Eom, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(g_eom, EINA_FALSE);

   g_eom->global = wl_global_create(e_comp_wl->wl.disp,
                           &wl_eom_interface,
                           1,
                           g_eom,
                           _e_eom_wl_bind_cb);

   EINA_SAFETY_ON_NULL_GOTO(g_eom->global, err);

   ret = _e_eom_init_internal();
   if (!ret)
     {
        EOM_ERR("failed init_internal()");
        goto err;
     }

   E_LIST_HANDLER_APPEND(g_eom->handlers, ECORE_DRM_EVENT_ACTIVATE, _e_eom_ecore_drm_activate_cb, g_eom);
   E_LIST_HANDLER_APPEND(g_eom->handlers, ECORE_DRM_EVENT_OUTPUT,   _e_eom_ecore_drm_output_cb,   g_eom);
   fullscreen_pre_hook = e_client_hook_add(E_CLIENT_HOOK_FULLSCREEN_PRE, _e_eom_fullscreen_pre_cb_hook, NULL);

   g_eom->is_external_init = 0;
   g_eom->is_internal_grab = 0;

   _e_eom_set_eom_attribute_state(WL_EOM_ATTRIBUTE_STATE_NONE);
   _e_eom_set_eom_attribute(WL_EOM_ATTRIBUTE_NONE);
   _e_eom_set_eom_status(WL_EOM_STATUS_NONE);
   _e_eom_set_eom_mode(WL_EOM_MODE_NONE);

   return EINA_TRUE;

err:
   _e_eom_deinit();
   return EINA_FALSE;
}

E_API void *
e_modapi_init(E_Module *m)
{
   return (_e_eom_init() ? m : NULL);
}

E_API int
e_modapi_shutdown(E_Module *m EINA_UNUSED)
{
   _e_eom_deinit();
   return 1;
}

E_API int
e_modapi_save(E_Module *m EINA_UNUSED)
{
   /* Save something to be kept */
   return 1;
}
