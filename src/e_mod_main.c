#define E_COMP_WL

#include <tdm.h>
#include <eom.h>
#include <tbm_bufmgr.h>
#include <tbm_surface.h>

#include "e.h"
#include "e_mod_main.h"
#include "eom-server-protocol.h"
#include "Ecore_Drm.h"

E_API E_Module_Api e_modapi = { E_MODULE_API_VERSION, "EOM Module" };
static E_EomEventData g_eom_event_data;
static E_EomFakeBuffers fake_buffers;
static E_EomPtr g_eom = NULL;

/* EOM Output Attributes

   +----------------+------------+----------------+-------------+
   |                |   normal   | exclusiv_share |  exclusive  |
   +----------------+------------+----------------+-------------+
   | normal         |  possible  |    possible    |  possible   |
   +----------------+------------+----------------+-------------+
   | exclusiv_share | impossible |    possible    |  possible   |
   +----------------+------------+----------------+-------------+
   | exclusive      | impossible |   impossible   | impossible  |
   +----------------+------------+----------------+-------------+

   possible   = 1
   impossible = 0
*/
static int eom_output_attributes[NUM_ATTR][NUM_ATTR] =
   {
      {1, 1, 1},
      {0, 1, 1},
      {0, 0, 0},
   };

static const char *eom_conn_types[] =
{
   "None", "VGA", "DVI-I", "DVI-D", "DVI-A",
   "Composite", "S-Video", "LVDS", "Component", "DIN",
   "DisplayPort", "HDMI-A", "HDMI-B", "TV", "eDP", "Virtual",
   "DSI",
};

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

static inline void
_e_eom_set_eom_attribute_by_current_client(enum wl_eom_attribute attribute)
{
   g_eom->eom_attribute = attribute;
}

static inline Eina_Bool
_e_eom_set_eom_attribute(enum wl_eom_attribute attribute)
{
   if (attribute == WL_EOM_ATTRIBUTE_NONE || g_eom->eom_attribute == WL_EOM_ATTRIBUTE_NONE)
     {
        g_eom->eom_attribute = attribute;
        return EINA_TRUE;
     }

   if (eom_output_attributes[g_eom->eom_attribute - 1][attribute - 1] == 1)
     {
        g_eom->eom_attribute = attribute;
        return EINA_TRUE;
     }

   return EINA_FALSE;
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
   tdm_error tdm_err = TDM_ERROR_NONE;
   E_EomEventDataPtr eom_data = NULL;

   RETURNIFTRUE(user_data == NULL, "ERROR: PP EVENT: user data is NULL");

   eom_data = (E_EomEventDataPtr)user_data;

   tdm_buffer_remove_release_handler(eom_data->dst_buffers[eom_data->pp_buffer],
                                     _e_eom_pp_cb, eom_data);

   /* Stop EOM */
   if (g_eom->eom_sate != UP)
     return;

   /* TODO: lock that flag??? */
   /* If a client has committed its buffer stop mirror mode */
   if (g_eom->is_mirror_mode == DOWN)
     return;

   tbm_surface_h src_buffer;
   src_buffer = _e_eom_root_internal_tdm_surface_get(g_eom->int_output_name);
   RETURNIFTRUE(src_buffer == NULL, "ERROR: PP EVENT: get root tdm surface");

   g_eom_event_data.pp_buffer = !g_eom_event_data.current_buffer;

   tdm_err = tdm_buffer_add_release_handler(g_eom_event_data.dst_buffers[g_eom_event_data.pp_buffer],
                                            _e_eom_pp_cb, &g_eom_event_data);
   RETURNIFTRUE(tdm_err != TDM_ERROR_NONE, "ERROR: PP EVENT: set pp hadler:%d", tdm_err );

   tdm_err = tdm_pp_attach(eom_data->pp, src_buffer, g_eom_event_data.dst_buffers[g_eom_event_data.pp_buffer]);
   RETURNIFTRUE(tdm_err != TDM_ERROR_NONE, "ERROR: pp attach:%d\n", tdm_err);

   tdm_err = tdm_pp_commit(g_eom_event_data.pp);
   RETURNIFTRUE(tdm_err != TDM_ERROR_NONE, "ERROR: PP EVENT: pp commit:%d", tdm_err );
}

static void
_e_eom_commit_cb(tdm_output *output EINA_UNUSED, unsigned int sequence EINA_UNUSED,
                           unsigned int tv_sec EINA_UNUSED, unsigned int tv_usec EINA_UNUSED,
                           void *user_data)
{
   E_EomClientBufferPtr client_buffer = NULL;
   E_EomEventDataPtr eom_data = NULL;
   tdm_error err = TDM_ERROR_NONE;

   RETURNIFTRUE(user_data == NULL, "ERROR: PP EVENT: user data is NULL");

   eom_data = (E_EomEventDataPtr)user_data;

   /* Stop EOM */
   if (g_eom->eom_sate != UP)
     return;

   /* TODO: Maybe better to separating that callback on to mirror and extended callbacks */
   if (g_eom->is_mirror_mode == UP)
     {
        if (eom_data->current_buffer == 1)
         {
            eom_data->current_buffer = 0;

            err = tdm_layer_set_buffer(eom_data->layer,
                                       eom_data->dst_buffers[!eom_data->pp_buffer]);
            RETURNIFTRUE(err != TDM_ERROR_NONE, "ERROR: EVENT: set buffer 0 err:%d", err);
         }
       else
         {
            eom_data->current_buffer = 1;

            err = tdm_layer_set_buffer(eom_data->layer,
                                       eom_data->dst_buffers[!eom_data->pp_buffer]);
            RETURNIFTRUE(err != TDM_ERROR_NONE, "ERROR: EVENT: set buffer 1 err:%d", err);
         }

       err = tdm_output_commit(eom_data->output, 0, _e_eom_commit_cb, eom_data);
       RETURNIFTRUE(err != TDM_ERROR_NONE, "ERROR: EVENT commit");
     }
   else
     {
        client_buffer = _e_eom_get_client_buffer_from_list();
        RETURNIFTRUE(client_buffer == NULL, "ERROR: EVENT: client buffer is NULL");

        err = tdm_layer_set_buffer(eom_data->layer, client_buffer->tbm_buffer);
        RETURNIFTRUE(err != TDM_ERROR_NONE, "ERROR: EVENT: set buffer 1");

        err = tdm_output_commit(eom_data->output, 0, _e_eom_commit_cb, eom_data);
        RETURNIFTRUE(err != TDM_ERROR_NONE, "ERROR: EVENT: commit");
     }
}

static void
_e_eom_deinit_external_output()
{
   tdm_error err = TDM_ERROR_NONE;
   int i = 0;

   if (g_eom_event_data.layer)
     {
        err = tdm_layer_unset_buffer(g_eom_event_data.layer);
        if (err != TDM_ERROR_NONE)
          EOM_DBG("EXT OUTPUT DEINIT: fail unset buffer:%d\n", err);
        else
          EOM_DBG("EXT OUTPUT DEINIT: ok unset buffer:%d\n", err);

        err = tdm_output_commit(g_eom_event_data.output, 0, NULL, &g_eom_event_data);
        if (err != TDM_ERROR_NONE)
          EOM_DBG ("EXT OUTPUT DEINIT: fail commit:%d\n", err);
        else
          EOM_DBG("EXT OUTPUT DEINIT: ok commit:%d\n", err);
    }

   /* TODO: do I need to do DPMS off? */
   err = tdm_output_set_dpms(g_eom_event_data.output, TDM_OUTPUT_DPMS_OFF);
   if (err != TDM_ERROR_NONE)
     EOM_ERR("EXT OUTPUT DEINIT: failed set DPMS off:%d\n", err);

   for (i = 0; i < NUM_MAIN_BUF; i++)
     {
        tdm_buffer_remove_release_handler(g_eom_event_data.dst_buffers[i],
                                          _e_eom_pp_cb, &g_eom_event_data);
        if (g_eom_event_data.dst_buffers[i])
          tbm_surface_destroy(g_eom_event_data.dst_buffers[i]);
    }

   if (g_eom->eom_sate == DOWN)
      _e_eom_client_buffers_list_free(g_eom->eom_clients);

   if (g_eom->int_output_name)
     {
        free(g_eom->int_output_name);
        g_eom->int_output_name = NULL;
     }

   if (g_eom->ext_output_name)
    {
       free(g_eom->ext_output_name);
       g_eom->ext_output_name = NULL;
    }

   if (g_eom->wl_output)
      g_eom->wl_output = NULL;
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
        RETURNVALIFTRUE(err != TDM_ERROR_NONE, NULL, "tdm_output_get_layer fail(%d)\n", err);

        err = tdm_layer_get_capabilities(layer, &capa);
        RETURNVALIFTRUE(err != TDM_ERROR_NONE, NULL, "tdm_layer_get_capabilities fail(%d)\n", err);

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
   RETURNVALIFTRUE(err != TDM_ERROR_NONE, NULL, "tdm_layer_set_info fail(%d)\n", err);

   return layer;
}

/* TODO: Models commited clients buffers */
/*
static void
_e_eom_create_fake_buffers(int width, int height)
{
   tbm_surface_info_s buffer_info;
   tbm_surface_h buffer = NULL;

   buffer = tbm_surface_internal_create_with_flags(width, height, TBM_FORMAT_ARGB8888, TBM_BO_SCANOUT);
   GOTOIFTRUE(buffer == NULL, err, "can not create fake_buffer\n");

   memset(&buffer_info, 0x0, sizeof(tbm_surface_info_s));
   if (tbm_surface_map(buffer,
                  TBM_SURF_OPTION_READ | TBM_SURF_OPTION_WRITE,
                  &buffer_info) != TBM_SURFACE_ERROR_NONE)
     {
        EOM_DBG("can not mmap fake_buffer\n");
        goto err;
     }

   memset(buffer_info.planes[0].ptr, 0xFF, buffer_info.planes[0].size);
   tbm_surface_unmap(buffer);

   fake_buffers.fake_buffers[0] = buffer;

err:
   return;
}
*/

static Eina_Bool
_e_eom_create_output_buffers(E_EomEventDataPtr eom_data, int width, int height)
{
   tbm_surface_info_s buffer_info;
   tbm_surface_h buffer = NULL;

   /*
    * TODO: Add support of other formats
    */
   buffer = tbm_surface_internal_create_with_flags(width, height, TBM_FORMAT_ARGB8888, TBM_BO_SCANOUT);
   GOTOIFTRUE(buffer == NULL, err, "can not create dst_buffer 1");

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

   memset(buffer_info.planes[0].ptr, 0x0, buffer_info.planes[0].size);
   tbm_surface_unmap(buffer);

   eom_data->dst_buffers[0] = buffer;

   /*
    * TODO: Add support of other formats
    */
   buffer = tbm_surface_internal_create_with_flags(width, height, TBM_FORMAT_ARGB8888, TBM_BO_SCANOUT);
   GOTOIFTRUE(buffer == NULL, err, "can not create dst_buffer 2");

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

   memset(buffer_info.planes[0].ptr, 0x0, buffer_info.planes[0].size);
   tbm_surface_unmap(buffer);

   eom_data->dst_buffers[1] = buffer;

   return EINA_TRUE;

err:

/*
 * Add deinitialization
 */
   return EINA_FALSE;
}

static enum wl_eom_type
_e_eom_output_name_to_eom_type(const char *output_name)
{
   enum wl_eom_type eom_type;

   if (output_name == NULL)
     return WL_EOM_TYPE_NONE;

   /* TODO: Add other external outputs */
   if (strcmp(output_name, "HDMI-A-0") == 0)
     eom_type = WL_EOM_TYPE_HDMIA;
   else
     eom_type = WL_EOM_TYPE_NONE;

   return eom_type;
}

static Eina_Bool
_e_eom_mirror_start(const char *output_name, int width, int height)
{
   /* should be changed in HWC enable environment */
   tbm_surface_info_s src_buffer_info;
   tbm_surface_h src_buffer = NULL;
   Eina_Bool ret = EINA_FALSE;

   src_buffer = _e_eom_root_internal_tdm_surface_get(output_name);
   RETURNVALIFTRUE(src_buffer == NULL, EINA_FALSE, "ERROR: get root tdm surfcae\n");

   tbm_surface_get_info(src_buffer, &src_buffer_info );

   EOM_DBG("FRAMEBUFFER TDM: %dx%d   bpp:%d   size:%d",
           src_buffer_info.width, src_buffer_info.height,
           src_buffer_info.bpp, src_buffer_info.size);

   /*
   g_eom->src_mode.w = width;
   g_eom->src_mode.h = height;
   g_eom->int_output_name = strdup(output_name);
   */

   /* TODO: if internal and external outputs are equal */
   ret = _e_eom_pp_is_needed(g_eom->src_mode.w, g_eom->src_mode.h,
                             g_eom->dst_mode.w, g_eom->dst_mode.h);
   RETURNVALIFTRUE(ret == EINA_FALSE, EINA_TRUE, "pp is not required\n");

   ret = _e_eom_pp_src_to_dst(src_buffer);
   RETURNVALIFTRUE(ret == EINA_FALSE, EINA_FALSE, "ERROR: init pp\n");

   return EINA_TRUE;
}

static tbm_surface_h
_e_eom_root_internal_tdm_surface_get(const char *name)
{
   Ecore_Drm_Output *primary_output = NULL;
   Ecore_Drm_Device *dev;
   const Eina_List *l;
#if 0
   Ecore_Drm_Fb *fb;
#else
   tdm_output *tdm_output_obj = NULL;
   tbm_surface_h tbm = NULL;
   tdm_error err = TDM_ERROR_NONE;
   int count = 0, i = 0;
#endif

   EINA_LIST_FOREACH(ecore_drm_devices_get(), l, dev)
     {
        primary_output = ecore_drm_device_output_name_find(dev, name);
        if (primary_output != NULL)
          break;
     }

#if 0
   RETURNVALIFTRUE(primary_output == NULL, NULL,
                   "ERROR: get primary output.(%s)\n",
                   name);

   /* I think it is more convenient than one upon, but E took first
    * output as primary and it can be not internal output
    *
   primary_output = ecore_drm_output_primary_get();
   RETURNVALIFTRUE(primary_output == NULL, NULL, "ERROR: get primary output\n");
   */
#else

   if (primary_output == NULL)
     {
        EOM_ERR("ERROR: get primary output.(%s)\n", name);
        EINA_LIST_FOREACH(ecore_drm_devices_get(), l, dev)
          {
             primary_output = ecore_drm_output_primary_get(dev);
             if (primary_output != NULL)
               break;
          }

        if (primary_output == NULL)
          {
             EOM_ERR("ERROR: get primary output.(%s)\n", name);
             return NULL;
          }
     }
#endif

#if 0
   fb = ecore_drm_display_output_primary_layer_fb_get(primary_output);
   RETURNVALIFTRUE(fb == NULL, NULL, "ERROR: get primary frambuffer\n");
   /*EOM_DBG("FRAMEBUFFER ECORE_DRM: is_client:%d mode%dx%d\n", fb->from_client, fb->w, fb->h);*/

   return (tbm_surface_h)fb->hal_buffer;
#else
   tdm_output_obj = tdm_display_get_output(g_eom->dpy, 0, &err);
   if (tdm_output_obj == NULL || err != TDM_ERROR_NONE)
     {
        EOM_ERR("tdm_display_get_output 0 fail\n");
        return NULL;
     }
   err = tdm_output_get_layer_count(tdm_output_obj, &count);
   if (err != TDM_ERROR_NONE)
     {
        EOM_ERR("tdm_output_get_layer_count fail\n");
        return NULL;
     }

   for (i = 0; i < count; i++)
     {
        tdm_layer *layer = tdm_output_get_layer(tdm_output_obj, i, NULL);
        tdm_layer_capability capabilities = 0;
        tdm_layer_get_capabilities(layer, &capabilities);
        if (capabilities & TDM_LAYER_CAPABILITY_PRIMARY)
          {
             tbm = tdm_layer_get_displaying_buffer(layer, &err);
             if (err != TDM_ERROR_NONE)
               {
                  EOM_ERR("tdm_layer_get_displaying_buffer fail\n");
                  return NULL;
               }
           break;
        }
     }

   return tbm;
#endif
}

static void
_e_eom_calculate_fullsize(int src_h, int src_v, int dst_size_h, int dst_size_v,
                          int *dst_x, int *dst_y, int *dst_w, int *dst_h)
{
   double h_ratio, v_ratio;

   h_ratio = src_h / dst_size_h;
   v_ratio = src_v / dst_size_v;

   if (h_ratio == v_ratio)
     {
        *dst_x = 0;
        *dst_y = 0;
        *dst_w = dst_size_h;
        *dst_h = dst_size_v;
     }
   else if (h_ratio < v_ratio)
     {
        *dst_y = 0;
        *dst_h = dst_size_v;
        *dst_w = dst_size_v * src_h / src_v;
        *dst_x = (dst_size_h - *dst_w) / 2;
     }
   else /* (h_ratio > v_ratio) */
     {
        *dst_x = 0;
        *dst_w = dst_size_h;
        *dst_h = dst_size_h * src_h / src_v;
        *dst_y = (dst_size_v - *dst_h) / 2;
     }
}

static Eina_Bool
_e_eom_pp_src_to_dst(tbm_surface_h src_buffer)
{
   tdm_error err = TDM_ERROR_NONE;
   tdm_info_pp pp_info;
   tdm_pp *pp = NULL;
   int x, y, w, h;

   pp = tdm_display_create_pp(g_eom->dpy, &err);
   RETURNVALIFTRUE(err != TDM_ERROR_NONE, EINA_FALSE, "ERROR: create pp:%d\n", err);

   /* TO DO : consider rotation */
   _e_eom_calculate_fullsize(g_eom->src_mode.w, g_eom->src_mode.h,
                             g_eom->dst_mode.w, g_eom->dst_mode.h,
                             &x, &y, &w, &h);
   EOM_DBG("x:%d, y:%d, w:%d, h:%d\n", x, y, w, h);

   g_eom_event_data.pp = pp;

   pp_info.src_config.size.h = g_eom->src_mode.w;
   pp_info.src_config.size.v = g_eom->src_mode.h;
   pp_info.src_config.pos.x = 0;
   pp_info.src_config.pos.y = 0;
   pp_info.src_config.pos.w = g_eom->src_mode.w;
   pp_info.src_config.pos.h = g_eom->src_mode.h;
   pp_info.src_config.format = TBM_FORMAT_ARGB8888;

   pp_info.dst_config.size.h = g_eom->dst_mode.w;
   pp_info.dst_config.size.v = g_eom->dst_mode.h;
   pp_info.dst_config.pos.x = x;
   pp_info.dst_config.pos.y = y;
   pp_info.dst_config.pos.w = w;
   pp_info.dst_config.pos.h = h;
   pp_info.dst_config.format = TBM_FORMAT_ARGB8888;

   /* TO DO : get rotation */
   pp_info.transform = TDM_TRANSFORM_NORMAL;
   pp_info.sync = 0;
   pp_info.flags = 0;

   err = tdm_pp_set_info(pp, &pp_info);
   RETURNVALIFTRUE(err != TDM_ERROR_NONE, EINA_FALSE, "ERROR: set pp info:%d\n", err);

   g_eom_event_data.pp_buffer = !g_eom_event_data.current_buffer;
   EOM_DBG("PP: curr:%d  pp:%d\n",
           g_eom_event_data.current_buffer,
           g_eom_event_data.pp_buffer);

   err = tdm_buffer_add_release_handler(g_eom_event_data.dst_buffers[g_eom_event_data.pp_buffer],
                                      _e_eom_pp_cb, &g_eom_event_data);
   RETURNVALIFTRUE(err != TDM_ERROR_NONE, EINA_FALSE, "ERROR: set pp hadler:%d\n", err);

   err = tdm_pp_attach(pp, src_buffer,
                       g_eom_event_data.dst_buffers[g_eom_event_data.pp_buffer]);
   RETURNVALIFTRUE(err != TDM_ERROR_NONE, EINA_FALSE, "ERROR: pp attach:%d\n", err);

   err = tdm_pp_commit(g_eom_event_data.pp);
   RETURNVALIFTRUE(err != TDM_ERROR_NONE, EINA_FALSE, "ERROR: pp commit:%d\n", err);

   return EINA_TRUE;
}

static Eina_Bool
_e_eom_pp_is_needed(int src_w, int src_h, int dst_w, int dst_h)
{
   if (src_w != dst_w)
     return EINA_TRUE;

   if (src_h != dst_h)
     return EINA_TRUE;

   return EINA_FALSE;
}

static Eina_Bool
_e_eom_ecore_drm_output_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Drm_Event_Output *e = NULL;
   char buff[PATH_MAX];

   if (!(e = event))  return ECORE_CALLBACK_PASS_ON;;

   EOM_DBG("id:%d (x,y,w,h):(%d,%d,%d,%d) (w_mm,h_mm):(%d,%d) refresh:%d subpixel_order:%d transform:%d make:%s model:%s name:%s plug:%d\n",
            e->id, e->x, e->y, e->w, e->h, e->phys_width, e->phys_height, e->refresh, e->subpixel_order, e->transform, e->make, e->model, e->name, e->plug);

   snprintf(buff, sizeof(buff), "%s", e->name);

   if (e->id == 0) /* main output */
     {
        if (e->plug == 1)
          {
              g_eom->src_mode.w = e->w;
              g_eom->src_mode.h = e->h;
              if (g_eom->int_output_name == NULL)
                g_eom->int_output_name = strdup(buff);

              g_eom->eom_sate = UP;
          }
        else
          {
             g_eom->src_mode.w = -1;
             g_eom->src_mode.h = -1;
             if (g_eom->int_output_name)
               free(g_eom->int_output_name);

             g_eom->eom_sate = DOWN;
          }
     }

   return ECORE_CALLBACK_PASS_ON;
}

static const tdm_output_mode *
_e_eom_get_best_mode(tdm_output *output)
{
   tdm_error ret = TDM_ERROR_NONE;
   const tdm_output_mode *modes;
   const tdm_output_mode *mode = NULL;
   /* unsigned int vrefresh = 0; */
   unsigned int best_value = 0;
   unsigned int value;
   int i, count = 0;

   ret = tdm_output_get_available_modes(output, &modes, &count);
   if (ret != TDM_ERROR_NONE)
     {
        EOM_ERR("tdm_output_get_available_modes fail(%d)\n", ret);
        return NULL;
     }
#if 0
   /* kernel error */
   for (i = 0; i < count; i++)
     {
        value = modes[i].vdisplay + modes[i].hdisplay;
        if (value > best_value)
          best_value = value;
     }

   for (i = 0; i < count; i++)
     {
        value = modes[i].vdisplay + modes[i].hdisplay;
        if (value != best_value)
          continue;

        if (modes[i].vrefresh > vrefresh)
          {
             mode = &modes[i];
             vrefresh = modes[i].vrefresh;
          }
     }
#else
   for (i = 0; i < count; i++)
     {
        value = modes[i].vdisplay + modes[i].hdisplay;
        if (value >= best_value)
          {
             best_value = value;
             mode = &modes[i];
          }
     }
#endif
   EOM_DBG("bestmode : %s, (%dx%d) r(%d), f(%d), t(%d)",
           mode->name, mode->hdisplay, mode->vdisplay, mode->vrefresh, mode->flags, mode->type);

   return mode;
}

static int
_e_eom_get_output_position(void)
{
   tdm_output *output_main = NULL;
   const tdm_output_mode *mode;
   tdm_error ret = TDM_ERROR_NONE;
   int x = 0;

   output_main = tdm_display_get_output(g_eom->dpy, 0, &ret);
   RETURNVALIFTRUE(ret != TDM_ERROR_NONE, 0, "tdm_display_get_output main fail(ret:%d)", ret);
   RETURNVALIFTRUE(output_main == NULL, 0, "tdm_display_get_output main fail(no output:%d)", ret);

   ret = tdm_output_get_mode(output_main, &mode);
   RETURNVALIFTRUE(ret != TDM_ERROR_NONE, 0, "tdm_output_get_mode main fail(ret:%d)", ret);

   if (mode == NULL)
     x = 0;
   else
     x = mode->hdisplay;

   if (g_eom->outputs)
     {
        Eina_List *l;
        E_EomOutputPtr eom_output_tmp;

        EINA_LIST_FOREACH(g_eom->outputs, l, eom_output_tmp)
          {
             if (eom_output_tmp->status != TDM_OUTPUT_CONN_STATUS_DISCONNECTED)
               x += eom_output_tmp->w;
          }
     }

   return x;
}

static void
_e_eom_start_mirror(E_EomOutputPtr eom_output, int width, int height)
{
   tdm_output *output;
   tdm_layer *hal_layer;
   tdm_info_layer layer_info;
   tdm_error tdm_err = TDM_ERROR_NONE;
   E_EomEventDataPtr eom_event_data = &g_eom_event_data;
   int ret = 0;

   if (eom_output->mirror_run == UP)
     return;

   output = eom_output->output;
   hal_layer = _e_eom_hal_layer_get(output, width, height);
   GOTOIFTRUE(hal_layer == NULL, err, "ERROR: get hal layer\n");

   ret = _e_eom_create_output_buffers(eom_event_data, width, height);
   GOTOIFTRUE(ret == EINA_FALSE, err, "ERROR: create buffers \n");

   tdm_err = tdm_layer_get_info(hal_layer, &layer_info);
   GOTOIFTRUE(tdm_err != TDM_ERROR_NONE, err, "ERROR: get layer info: %d", tdm_err);

   EOM_DBG("LAYER INFO: %dx%d, pos (x:%d, y:%d, w:%d, h:%d,  dpos (x:%d, y:%d, w:%d, h:%d))",
           layer_info.src_config.size.h,  layer_info.src_config.size.v,
           layer_info.src_config.pos.x, layer_info.src_config.pos.y,
           layer_info.src_config.pos.w, layer_info.src_config.pos.h,
           layer_info.dst_pos.x, layer_info.dst_pos.y,
           layer_info.dst_pos.w, layer_info.dst_pos.h);

   g_eom->dst_mode.w = width;
   g_eom->dst_mode.h = height;
   /* TODO: free that memory */
   /*g_eom->ext_output_name = strdup(output_name);*/

   eom_event_data->layer = hal_layer;
   eom_event_data->output = output;
   eom_event_data->current_buffer = 0;

   tdm_err = tdm_layer_set_buffer(hal_layer, eom_event_data->dst_buffers[eom_event_data->current_buffer]);
   GOTOIFTRUE(tdm_err != TDM_ERROR_NONE, err, "ERROR: set buffer on layer:%d\n", tdm_err);

   g_eom->is_external_init = 1;
   g_eom->id = eom_output->id;

   tdm_err = tdm_output_set_dpms(output, TDM_OUTPUT_DPMS_ON);
   GOTOIFTRUE(tdm_err != TDM_ERROR_NONE, err, "ERROR: tdm_output_set_dpms on\n");

   /* get main surface */
   ret = _e_eom_mirror_start(g_eom->int_output_name, g_eom->src_mode.w, g_eom->src_mode.h);
   GOTOIFTRUE(ret == EINA_FALSE, err, "ERROR: get root surfcae\n");

   tdm_err = tdm_output_commit(output, 0, _e_eom_commit_cb, &g_eom_event_data);
   GOTOIFTRUE(tdm_err != TDM_ERROR_NONE, err, "ERROR: commit crtc:%d\n", tdm_err);

   _e_eom_set_eom_mode(WL_EOM_MODE_MIRROR);

   eom_output->mirror_run = UP;

   return;

err:
/*
 * TODO: add deinitialization
 */
   return;
}

static void
_e_eom_stop_mirror(E_EomOutputPtr eom_output)
{
   if (eom_output->mirror_run == DOWN)
     return;

   g_eom->is_external_init = 0;
   g_eom->is_internal_grab = 0;
   g_eom->id = -1;

   _e_eom_set_eom_status(WL_EOM_STATUS_DISCONNECTION);
   _e_eom_set_eom_mode(WL_EOM_MODE_NONE);

   _e_eom_deinit_external_output();

   eom_output->mirror_run = DOWN;
}

static void
_e_eom_tdm_output_status_change_cb(tdm_output *output, tdm_output_change_type type, tdm_value value, void *user_data)
{
   tdm_output_type tdm_type;
   tdm_error ret = TDM_ERROR_NONE;
   tdm_output_conn_status status;
   tdm_output_conn_status status2;
   const char *maker = NULL, *model = NULL, *name = NULL;
   const char *tmp_name;
   char new_name[DRM_CONNECTOR_NAME_LEN];
   E_EomOutputPtr eom_output = NULL;
   tdm_output_conn_status plug;

   if (type == TDM_OUTPUT_CHANGE_DPMS)
     return;

   if (g_eom->outputs)
     {
        Eina_List *l;
        E_EomOutputPtr eom_output_tmp;

        EINA_LIST_FOREACH(g_eom->outputs, l, eom_output_tmp)
          {
             if (eom_output_tmp->output == output)
               eom_output = eom_output_tmp;
          }
     }

   ret = tdm_output_get_output_type(output, &tdm_type);
   RETURNIFTRUE(ret != TDM_ERROR_NONE, "tdm_output_get_output_type fail(%d)", ret);

   ret = tdm_output_get_model_info(output, &maker, &model, &name);
   RETURNIFTRUE(ret != TDM_ERROR_NONE, "tdm_output_get_model_info fail(%d)", ret);

   ret = tdm_output_get_conn_status(output, &status);
   RETURNIFTRUE(ret != TDM_ERROR_NONE, "tdm_output_get_conn_status fail(%d)", ret);

   status2 = value.u32;

   EOM_DBG("type(%d, %d), status(%d, %d) (%s,%s,%s)", type, tdm_type, status, status2, maker, model, name);

   if (tdm_type < ALEN(eom_conn_types))
     tmp_name = eom_conn_types[tdm_type];
   else
     tmp_name = "unknown";
   snprintf(new_name, sizeof(new_name), "%s-%d", tmp_name, 0);

   plug = value.u32;

   if (plug == TDM_OUTPUT_CONN_STATUS_CONNECTED)
     {
        unsigned int mmWidth, mmHeight, subpixel;
        const tdm_output_mode *mode;
        int x = 0;

        ret = tdm_output_get_physical_size(output, &mmWidth, &mmHeight);
        RETURNIFTRUE(ret != TDM_ERROR_NONE, "tdm_output_get_physical_size fail(%d)", ret);

        ret = tdm_output_get_subpixel(output, &subpixel);
        RETURNIFTRUE(ret != TDM_ERROR_NONE, "tdm_output_get_subpixel fail(%d)", ret);

        mode = _e_eom_get_best_mode(output);
        RETURNIFTRUE(mode == NULL, "_e_eom_get_best_resolution fail");

        ret = tdm_output_set_mode(output, mode);
        RETURNIFTRUE(ret != TDM_ERROR_NONE, "tdm_output_set_mode fail(%d)", ret);

        x = _e_eom_get_output_position();
        EOM_DBG("mode: %dx%d, phy(%dx%d), pos(%d,0), refresh:%d, subpixel:%d",
                mode->hdisplay, mode->vdisplay, mmWidth, mmHeight, x, mode->vrefresh, subpixel);

        if (!e_comp_wl_output_init(new_name, maker, model, x, 0,
                                   mode->hdisplay, mode->vdisplay,
                                   mmWidth, mmHeight, mode->vrefresh, subpixel, 0))
          {
             EOM_ERR("Could not setup new output: %s", new_name);
             return;
          }
        EOM_DBG("Setup new output: %s", new_name);

        /* update eom_output connect */
        eom_output->w = mode->hdisplay;
        eom_output->h = mode->vdisplay;
        eom_output->phys_width = mmWidth;
        eom_output->phys_height = mmHeight;
        eom_output->status = plug;

        g_eom->is_mirror_mode = UP;
        g_eom->eom_sate = UP;

        /* TODO: check output mode(presentation set) and HDMI type */
        _e_eom_start_mirror(eom_output, mode->hdisplay, mode->vdisplay);
     }
   else if (TDM_OUTPUT_CONN_STATUS_DISCONNECTED)
     {
        if (eom_output->mirror_run == UP)
          _e_eom_stop_mirror(eom_output);

        /* update eom_output disconnect */
        eom_output->w = 0;
        eom_output->h = 0;
        eom_output->phys_width = 0;
        eom_output->phys_height = 0;
        eom_output->status = plug;

        g_eom->is_mirror_mode = DOWN;
        g_eom->eom_sate = DOWN;

        e_comp_wl_output_remove(new_name);
        EOM_DBG("Destory output: %s", new_name);
     }
}

static Eina_Bool
_e_eom_client_buffer_change(void *data, int type, void *event)
{
   E_Comp_Wl_Buffer *external_wl_buffer = NULL;
   E_EomClientBufferPtr client_buffer = NULL;
   E_Event_Client *ev = event;
   E_Client *ec = NULL;
   /* Eina_Bool ret_err; */
   /*
   tbm_surface_h external_tbm_buffer = NULL;
   tbm_surface_info_s surface_info;
   int ret;
   */

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   return ECORE_CALLBACK_PASS_ON;

   ec = ev->ec;
   if (e_object_is_del(E_OBJECT(ec)))
     {
        EOM_ERR("ERROR: BUFF CHANGE: ec objects is del\n");
        return ECORE_CALLBACK_PASS_ON;
     }

   /* TODO: Make all goto same, not err, end, ret etc. */
   /* We are not interested in non external clients */
   /*
   ret_err = e_client_is_external_output_client(ec);
   RETURNVALIFTRUE(ret_err != EINA_TRUE,
                   ECORE_CALLBACK_PASS_ON,
                   "ERROR: BUFF CHANGE: ec is not external\n");
   */

   if (ec->pixmap == NULL)
     return ECORE_CALLBACK_PASS_ON;

   external_wl_buffer = e_pixmap_resource_get(ec->pixmap);
   RETURNVALIFTRUE(external_wl_buffer == NULL,
                   ECORE_CALLBACK_PASS_ON,
                   "ERROR:BUFF CHANGE: wl buffer is NULL\n");

   EOM_DBG("BUFF CHANGE: wl_buff:%dx%d",
            external_wl_buffer->w,
            external_wl_buffer->h);

   if (external_wl_buffer->w == 1 && external_wl_buffer->h == 1)
     {
        EOM_ERR("ERROR:BUFF CHANGE: skip first 1x1 client buffer\n");
        return ECORE_CALLBACK_PASS_ON;
     }

   /*TODO: wayland_tbm_server_get_surface is implicit declarated */
   /*external_tbm_buffer = wayland_tbm_server_get_surface(NULL,
                              external_wl_buffer->resource);
   if (external_tbm_buffer == NULL)
     {
        EOM_ERR("ERROR: BUFF CHANGE: client tbm buffer is NULL\n");
        return ECORE_CALLBACK_PASS_ON;
     }

   EOM_DBG("BUFF CHANGE: tbm_buffer %p", external_tbm_buffer);
   */

   /* mmap that buffer to get width and height for test's sake */
   /*
   memset(&surface_info, 0, sizeof(tbm_surface_info_s));
   ret = tbm_surface_map(external_tbm_buffer, TBM_SURF_OPTION_READ |
                       TBM_SURF_OPTION_WRITE, &surface_info);
   if (ret != TBM_SURFACE_ERROR_NONE)
     {
        EOM_ERR("BUFF CHANGE: failed mmap buffer: %d", ret);
        //return ECORE_CALLBACK_PASS_ON;
     }

   EOM_DBG("BUFF CHANGE: tbm_buffer: %dx%d", surface_info.width, surface_info.height);

   tbm_surface_unmap(external_tbm_buffer);
   */

   /* TODO: Must find proper way of getting tbm_surface */
   /*client_buffer = _e_eom_create_client_buffer(external_wl_buffer, external_tbm_buffer);*/
   client_buffer = _e_eom_create_client_buffer(external_wl_buffer, fake_buffers.fake_buffers[0]);
   RETURNVALIFTRUE(client_buffer == NULL,
                   ECORE_CALLBACK_PASS_ON,
                   "ERROR: BUFF CHANGE: alloc client buffer");

   _e_eom_add_client_buffer_to_list(client_buffer);

   /* Stop mirror mode */
   g_eom->is_mirror_mode = DOWN;

   return ECORE_CALLBACK_PASS_ON;

   /* TODO: deinitialization */
}

static void
_e_eom_add_client_buffer_to_list(E_EomClientBufferPtr client_buffer)
{
   _e_eom_client_buffers_list_free();

   g_eom_event_data.client_buffers_list = eina_list_append(g_eom_event_data.client_buffers_list, client_buffer);
}

static void
_e_eom_client_buffers_list_free()
{
   E_EomClientBufferPtr *buffer = NULL;
   Eina_List *l;

   EINA_LIST_FOREACH(g_eom_event_data.client_buffers_list, l, buffer)
     {
        if (buffer)
          {
             /* I am not sure if it is necessary */
             /* tbm_surface_internal_unref(buffer->tbm_buffer); */

             /* TODO: Do we need reference that buffer? */
             /*e_comp_wl_buffer_reference(buffer->tbm_buffer, NULL);*/

             g_eom_event_data.client_buffers_list = eina_list_remove(g_eom_event_data.client_buffers_list, buffer);
             E_FREE(buffer);
          }
     }
}

static E_EomClientBufferPtr
_e_eom_create_client_buffer(E_Comp_Wl_Buffer *wl_buffer, tbm_surface_h tbm_buffer)
{
   E_EomClientBufferPtr buffer = NULL;

   buffer = E_NEW(E_EomClientBuffer, 1);
   if(buffer == NULL)
      return NULL;

   buffer->wl_buffer = wl_buffer;
   buffer->tbm_buffer = tbm_buffer;
   /* TODO: It is not used right now */
   buffer->stamp = _e_eom_get_time_in_mseconds();

   /* I am not sure if it is necessary */
   /* tbm_surface_internal_ref(tbm_buffer); */

   /* TODO: Do we need reference that buffer? */
   /*e_comp_wl_buffer_reference(buffer->tbm_buffer, NULL);*/

   return buffer;
}

static E_EomClientBufferPtr
_e_eom_get_client_buffer_from_list()
{
   E_EomClientBufferPtr buffer = NULL;
   Eina_List *l;

   /* There must be only one buffer */
   EINA_LIST_FOREACH(g_eom_event_data.client_buffers_list, l, buffer)
     {
        if (buffer)
          return buffer;
     }

   return NULL;
}

static int
_e_eom_get_time_in_mseconds()
{
   struct timespec tp;

   clock_gettime(CLOCK_MONOTONIC, &tp);

   return ((tp.tv_sec * 1000) + (tp.tv_nsec / 1000));
}

static Eina_Bool
_e_eom_ecore_drm_activate_cb(void *data, int type EINA_UNUSED, void *event)
{
 /*
   Ecore_Drm_Event_Activate *e = NULL;
   E_EomPtr eom = NULL;

   EOM_DBG("_e_eom_ecore_drm_activate_cb called\n");

   if ((!event) || (!data)) goto end;
   e = event;
   eom = data;

   EOM_DBG("e->active:%d\n", e->active);

   if (e->active)
     {
        ;
     }
   else
     {
        ;
     }

end:

*/
   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_eom_wl_request_set_attribute_cb(struct wl_client *client, struct wl_resource *resource, uint32_t output_id, uint32_t attribute)
{
   enum wl_eom_error eom_error = WL_EOM_ERROR_NONE;
   struct wl_resource *iterator = NULL;
   Eina_Bool changes = EINA_FALSE;
   Eina_Bool ret = EINA_FALSE;
   Eina_List *l;

   if (resource == g_eom->current_client)
     {
        /* Current client can set any flag it wants */
        _e_eom_set_eom_attribute_by_current_client(attribute);
        changes = EINA_TRUE;
     }
   else
     {
        ret = _e_eom_set_eom_attribute(attribute);
        if (ret == EINA_FALSE)
          {
             EOM_DBG("set attribute FAILED\n");

             eom_error = WL_EOM_ERROR_OUTPUT_OCCUPIED;
             goto end;
          }

        changes = EINA_TRUE;
     }

   EOM_DBG("set attribute OK\n");

   /* If client has set WL_EOM_ATTRIBUTE_NONE, eom will be
    * switched to mirror mode
    */
   if (attribute == WL_EOM_ATTRIBUTE_NONE && g_eom->is_mirror_mode == DOWN)
     {
        g_eom->is_mirror_mode = UP;
        ret = _e_eom_set_eom_attribute(WL_EOM_ATTRIBUTE_NONE);
        _e_eom_set_eom_mode(WL_EOM_MODE_MIRROR);

        _e_eom_client_buffers_list_free();

        ret = _e_eom_mirror_start(g_eom->int_output_name,
                                  g_eom->src_mode.w,
                                  g_eom->src_mode.h);
        GOTOIFTRUE(ret == EINA_FALSE,
                   end,
                   "ERROR: restore mirror mode after a client disconnection\n");

        goto end;
     }

end:
   wl_eom_send_output_attribute(resource,
                                g_eom->id,
                                _e_eom_get_eom_attribute(),
                                _e_eom_get_eom_attribute_state(),
                                eom_error);

   /* Notify eom clients that eom state has been changed */
   if (changes == EINA_TRUE)
     {
        EINA_LIST_FOREACH(g_eom->eom_clients, l, iterator)
          {
             if (iterator == resource)
               continue;

             if (iterator)
               {
                 if (g_eom->is_mirror_mode == UP)
                   wl_eom_send_output_attribute(iterator,
                                                g_eom->id,
                                                _e_eom_get_eom_attribute(),
                                                _e_eom_get_eom_attribute_state(),
                                                WL_EOM_ERROR_NONE);
                 else
                   wl_eom_send_output_attribute(iterator,
                                                g_eom->id,
                                                _e_eom_get_eom_attribute(),
                                                WL_EOM_ATTRIBUTE_STATE_LOST,
                                                WL_EOM_ERROR_NONE);
               }
          }

        g_eom->current_client = resource;
     }

   return;
}

static void
_e_eom_wl_request_get_output_info_cb(struct wl_client *client, struct wl_resource *resource, uint32_t output_id)
{
   EOM_DBG("output:%d\n", output_id);

   if (g_eom->outputs)
     {
        Eina_List *l;
        E_EomOutputPtr output = NULL;

        EINA_LIST_FOREACH(g_eom->outputs, l, output)
          {
             if (output->id == output_id)
               {
                  EOM_DBG("send - id : %d, type : %d, mode : %d, w : %d, h : %d, w_mm : %d, h_mm : %d, conn : %d\n",
                          output->id, output->type, output->mode, output->w, output->h,
                          output->phys_width, output->phys_height, output->status);

                  wl_eom_send_output_info(resource, output->id, output->type, output->mode, output->w, output->h,
                                          output->phys_width, output->phys_height, output->status);
               }
          }
     }
}

static const struct wl_eom_interface _e_eom_wl_implementation =
{
   _e_eom_wl_request_set_attribute_cb,
   _e_eom_wl_request_get_output_info_cb
};

/* wl_eom global object destroy function */
static void
_e_eom_wl_resource_destory_cb(struct wl_resource *resource)
{
   struct wl_resource *iterator = NULL;
   Eina_List *l = NULL;
   Eina_Bool ret;

   EOM_DBG("client unbind\n");

   g_eom->eom_clients = eina_list_remove(g_eom->eom_clients, resource);

   /* If not current client has been destroyed do nothing */
   if (resource != g_eom->current_client)
     goto end2;

   /* If a client has been disconnected and mirror mode has not
    * been restore, start mirror mode
    */
   if (g_eom->is_mirror_mode == DOWN)
     {
        g_eom->is_mirror_mode = UP;
        ret = _e_eom_set_eom_attribute(WL_EOM_ATTRIBUTE_NONE);
        _e_eom_set_eom_mode(WL_EOM_MODE_MIRROR);

        _e_eom_client_buffers_list_free();

        ret = _e_eom_mirror_start(g_eom->int_output_name,
                                  g_eom->src_mode.w,
                                  g_eom->src_mode.h);
        GOTOIFTRUE(ret == EINA_FALSE,
                   end,
                   "ERROR: restore mirror mode after a client disconnection\n");
     }

end:
   /* Notify eom clients that eom state has been changed */
   EINA_LIST_FOREACH(g_eom->eom_clients, l, iterator)
     {
        if (iterator)
          {
             wl_eom_send_output_attribute(iterator,
                                          g_eom->id,
                                          _e_eom_get_eom_attribute(),
                                          _e_eom_get_eom_attribute_state(),
                                          WL_EOM_ERROR_NONE);
          }
     }

end2:
   g_eom->current_client = NULL;
}

/* wl_eom global object bind function */
static void
_e_eom_wl_bind_cb(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   enum wl_eom_type eom_type = WL_EOM_TYPE_NONE;
   struct wl_resource *resource = NULL;

   RETURNIFTRUE(data == NULL, "ERROR: data is NULL");

   E_EomPtr eom = data;

   resource = wl_resource_create(client,
                         &wl_eom_interface,
                         MIN(version, 1),
                         id);
   if (resource == NULL)
     {
        EOM_ERR("error. resource is null. (version :%d, id:%d)\n", version, id);
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(resource,
                                  &_e_eom_wl_implementation,
                                  eom,
                                  _e_eom_wl_resource_destory_cb);

   eom_type = _e_eom_output_name_to_eom_type(g_eom->ext_output_name);

   wl_eom_send_output_type(resource,
                           eom->id,
                           eom_type,
                           _e_eom_get_eom_status());

   wl_eom_send_output_attribute(resource,
                                eom->id,
                                _e_eom_get_eom_attribute(),
                                _e_eom_get_eom_attribute_state(),
                                WL_EOM_ERROR_NONE);

   wl_eom_send_output_mode(resource,
                           eom->id,
                           _e_eom_get_eom_mode());

   EOM_DBG("send - output count : %d\n", g_eom->output_count);
   wl_eom_send_output_count(resource,
                            g_eom->output_count);

   if (g_eom->outputs)
     {
        Eina_List *l;
        E_EomOutputPtr output = NULL;

        EINA_LIST_FOREACH(g_eom->outputs, l, output)
          {
             EOM_DBG("send - id : %d, type : %d, mode : %d, w : %d, h : %d, w_mm : %d, h_mm : %d, conn : %d\n",
                     output->id, output->type, output->mode, output->w, output->h,
                     output->phys_width, output->phys_height, output->status);
             wl_eom_send_output_info(resource, output->id, output->type, output->mode, output->w, output->h,
                                     output->phys_width, output->phys_height, output->status);
          }
     }

   g_eom->eom_clients = eina_list_append(g_eom->eom_clients, resource);
}

static void
_e_eom_deinit()
{
   Ecore_Event_Handler *h = NULL;

   if (g_eom == NULL) return;

   if (g_eom->handlers)
     {
        EINA_LIST_FREE(g_eom->handlers, h)
        ecore_event_handler_del(h);
     }

   if (g_eom->dpy) tdm_display_deinit(g_eom->dpy);
   if (g_eom->bufmgr) tbm_bufmgr_deinit(g_eom->bufmgr);

   if (g_eom->global) wl_global_destroy(g_eom->global);

   E_FREE(g_eom);
}

static Eina_Bool
_e_eom_output_info_get(tdm_display *dpy)
{
   tdm_error ret = TDM_ERROR_NONE;
   int i, count;

   ret = tdm_display_get_output_count(dpy, &count);
   RETURNVALIFTRUE(ret != TDM_ERROR_NONE,
                   EINA_FALSE,
                   "tdm_display_get_output_count fail");
   RETURNVALIFTRUE(count <= 1,
                   EINA_FALSE,
                   "output count is 1. device doesn't support external outputs.\n");

   g_eom->output_count = count - 1;
   EOM_DBG("external output count : %d\n", g_eom->output_count);

   /* skip main output id:0 */
   /* start from 1 */
   for (i = 1; i < count; i++)
     {
        const tdm_output_mode *mode = NULL;
        E_EomOutputPtr new_output = NULL;
        unsigned int mmWidth, mmHeight;
        tdm_output_conn_status status;
        tdm_output *output = NULL;
        tdm_output_type type;

        output = tdm_display_get_output(dpy, i, &ret);
        GOTOIFTRUE(ret != TDM_ERROR_NONE,
                   err,
                   "tdm_display_get_output fail(ret:%d)", ret);

        GOTOIFTRUE(output == NULL,
                   err,
                   "tdm_display_get_output fail(no output:%d)", ret);

        ret = tdm_output_get_output_type(output, &type);
        GOTOIFTRUE(ret != TDM_ERROR_NONE,
                   err,
                   "tdm_output_get_output_type fail(%d)", ret);

        new_output = E_NEW(E_EomOutput, 1);
        GOTOIFTRUE(new_output == NULL,
                   err,
                   "calloc fail");

        ret = tdm_output_get_conn_status(output, &status);
        if (ret != TDM_ERROR_NONE)
          {
             EOM_ERR("tdm_output_get_conn_status fail(%d)", ret);
             free(new_output);
             goto err;
          }
        new_output->id = i;
        new_output->type = type;
        new_output->status = status;
        new_output->mode = EOM_OUTPUT_MODE_NONE;
        new_output->output = output;

        ret = tdm_output_add_change_handler(output, _e_eom_tdm_output_status_change_cb, NULL);
        if (ret != TDM_ERROR_NONE)
          {
              EOM_ERR("tdm_output_add_change_handler fail(%d)", ret);
              free(new_output);
              goto err;
          }

        if (status == TDM_OUTPUT_CONN_STATUS_DISCONNECTED)
          {
             EOM_DBG("create(%d)output, type:%d, status:%d",
                     new_output->id, new_output->type, new_output->status);
             g_eom->outputs = eina_list_append(g_eom->outputs, new_output);
             continue;
          }
        new_output->status = TDM_OUTPUT_CONN_STATUS_CONNECTED;

        ret = tdm_output_get_mode(output, &mode);
        if (ret != TDM_ERROR_NONE)
          {
             EOM_ERR("tdm_output_get_mode fail(%d)", ret);
             free(new_output);
             goto err;
          }

        if (mode == NULL)
          {
             new_output->w = 0;
             new_output->h = 0;
          }

        else
          {
             new_output->w = mode->hdisplay;
             new_output->h = mode->vdisplay;
          }

        ret = tdm_output_get_physical_size(output, &mmWidth, &mmHeight);
        if (ret != TDM_ERROR_NONE)
          {
             EOM_ERR("tdm_output_get_conn_status fail(%d)", ret);
             free(new_output);
             goto err;
          }
        new_output->phys_width = mmWidth;
        new_output->phys_height = mmHeight;

        EOM_DBG("create(%d)output, type:%d, status:%d, w:%d, h:%d, mm_w:%d, mm_h:%d",
                new_output->id, new_output->type, new_output->status,
                new_output->w, new_output->h, new_output->phys_width, new_output->phys_height);

        g_eom->outputs = eina_list_append(g_eom->outputs, new_output);
     }

   return EINA_TRUE;

err:
   if (g_eom->outputs)
     {
        Eina_List *l;
        E_EomOutputPtr output;

        EINA_LIST_FOREACH(g_eom->outputs, l, output)
          {
             free(output);
          }
        eina_list_free(g_eom->outputs);
     }

   return EINA_FALSE;
}

static Eina_Bool
_e_eom_init_internal()
{
   tdm_error ret = TDM_ERROR_NONE;

   g_eom->dpy = tdm_display_init(&ret);
   GOTOIFTRUE(ret != TDM_ERROR_NONE, err, "tdm_display_init fail");

   ret = tdm_display_get_fd(g_eom->dpy, &g_eom->fd);
   GOTOIFTRUE(ret != TDM_ERROR_NONE, err, "tdm_display_get_fd fail");

   g_eom->bufmgr = tbm_bufmgr_init(g_eom->fd);
   GOTOIFTRUE(g_eom->bufmgr == NULL, err, "tbm_bufmgr_init fail");

   if (_e_eom_output_info_get(g_eom->dpy) != EINA_TRUE)
     {
        EOM_ERR("_e_eom_output_info_get fail\n");
        goto err;
     }

   return EINA_TRUE;

err:
   if (g_eom->bufmgr)
     tbm_bufmgr_deinit(g_eom->bufmgr);

   if (g_eom->dpy)
     tdm_display_deinit(g_eom->dpy);

   return EINA_FALSE;
}

static Eina_Bool
_e_eom_init()
{
   Eina_Bool ret = EINA_FALSE;

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
   GOTOIFTRUE(ret == EINA_FALSE, err, "failed init_internal()");

   E_LIST_HANDLER_APPEND(g_eom->handlers, ECORE_DRM_EVENT_ACTIVATE, _e_eom_ecore_drm_activate_cb, g_eom);
   E_LIST_HANDLER_APPEND(g_eom->handlers, ECORE_DRM_EVENT_OUTPUT, _e_eom_ecore_drm_output_cb, g_eom);
   E_LIST_HANDLER_APPEND(g_eom->handlers, E_EVENT_CLIENT_BUFFER_CHANGE, _e_eom_client_buffer_change, NULL);

   g_eom->is_external_init = DOWN;
   g_eom->is_internal_grab = DOWN;
   g_eom->ext_output_name = NULL;
   g_eom->int_output_name = NULL;

   g_eom->current_client = NULL;

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
