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

typedef struct _E_Eom E_Eom, *E_EomPtr;

struct _E_Eom
{
   struct wl_global *global;
   struct wl_resource *resource;
   Eina_List *handlers;

   tdm_display *dpy;
   tbm_bufmgr bufmgr;
   int fd;
};

typedef struct _Ecore_Drm_Hal_Output
{
   tdm_output *output;
   tdm_layer *primary_layer;
} Ecore_Drm_Hal_Output;

typedef struct
{
   tdm_layer *layer;
   tdm_output *output;
   tbm_surface_h dst_buffers;
} Eom_Event;

static Eom_Event g_eom_event;

E_EomPtr g_eom = NULL;

E_API E_Module_Api e_modapi = { E_MODULE_API_VERSION, "EOM Module" };

static E_Client_Hook *fullscreen_pre_hook = NULL;

static E_Comp_Wl_Output *
_e_eom_e_comp_wl_output_get(Eina_List *outputs, const char *id)
{
   Eina_List *l;
   E_Comp_Wl_Output *output = NULL, *out;
   int num_outputs = 0;

   EINA_LIST_FOREACH(outputs, l, out)
     {
       char *temp_id = NULL;
       temp_id = strchr(out->id, '/');
       if (temp_id == NULL)
         {
           if (!strcmp(out->id, id))
             output = out;
         }
       else
         {
           int loc = temp_id - out->id;

           if (!strncmp(out->id, id, loc))
             output = out;
         }

       num_outputs += 1;
     }

   /*
    * There is no external output
    */
   if (num_outputs == 1)
     return NULL;

   return output;
}

static tdm_output *
_e_eom_hal_output_get(const char *id, int primary_output_id)
{
   Ecore_Drm_Output *drm_output;
   Ecore_Drm_Device *dev;
   Eina_List *l;
   int crtc_id = 0;
   tdm_error err = TDM_ERROR_NONE;

   tdm_output *output = NULL;


   /*
    * Temporary take into account only HDMI
    */
   if (strcmp(id, "HDMI-A-0"))
     {
       EOM_DBG("not find output\n");
       return NULL;
     }

   EINA_LIST_FOREACH(ecore_drm_devices_get(), l, dev)
     {
       drm_output = ecore_drm_device_output_name_find(dev, id);
     }

   if (!drm_output)
     {
       EOM_DBG("not find drm output\n");
       return NULL;
     }

   crtc_id = ecore_drm_output_crtc_id_get(drm_output);
   if (crtc_id == 0)
    {
      EOM_DBG("crtc is 0\n");
      return NULL;
    }

   output = tdm_display_get_output(g_eom->dpy, crtc_id, NULL);
   if (!output)
     {
       EOM_DBG("there is no HAL output for:%d\n", crtc_id);
       return NULL;
     }

   EOM_DBG("find\n");
   return output;
}

static tdm_layer *
_e_eom_hal_layer_get(tdm_output *output, int width, int height)
{
   int i = 0;
   int count = 0;
   tdm_layer *layer;
   tdm_error err = TDM_ERROR_NONE;
   tdm_layer_capability capa;

   tdm_info_layer set_layer_info;

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

   memset(&set_layer_info, 0x0, sizeof(tdm_info_layer));
      set_layer_info.src_config.size.h = width;
      set_layer_info.src_config.size.v = height;
      set_layer_info.src_config.pos.x = 0;
      set_layer_info.src_config.pos.y = 0;
      set_layer_info.src_config.pos.w = width;
      set_layer_info.src_config.pos.h = height;
      set_layer_info.src_config.format = TBM_FORMAT_ARGB8888;
      set_layer_info.dst_pos.x = 0;
      set_layer_info.dst_pos.y = 0;
      set_layer_info.dst_pos.w = width;
      set_layer_info.dst_pos.h = height;
      set_layer_info.transform = TDM_TRANSFORM_NORMAL;

      err = tdm_layer_set_info(layer, &set_layer_info);
      if (err != TDM_ERROR_NONE)
        {
          EOM_DBG ("tdm_layer_set_info fail(%d)\n", err);
          return NULL;
        }

   return layer;
}

static tbm_surface_h
_e_eom_root_window_tdm_surface_get()
{
   Evas_Engine_Info_Drm *einfo;
   Ecore_Drm_Fb* fb;

   if (!e_comp || !e_comp->evas)
     return NULL;

   einfo = (Evas_Engine_Info_Drm *)evas_engine_info_get(e_comp->evas);

   fb = _ecore_drm_display_fb_find_with_id(einfo->info.buffer_id);
   if (!fb)
     {
       EOM_DBG("no Ecore_Drm_Fb for dci_output_id:%d\n", einfo->info.buffer_id);
       return NULL;
     }

   if (!fb->hal_buffer)
     {
       EOM_DBG("no hal_buffer\n");
       return NULL;
     }

   EOM_DBG("find hal_buffer");
   return fb->hal_buffer;
}

static void
_e_eom_output_cb_commit(tdm_output *output EINA_UNUSED, unsigned int sequence EINA_UNUSED,
                           unsigned int tv_sec EINA_UNUSED, unsigned int tv_usec EINA_UNUSED,
                           void *user_data)
{
   Eom_Event *eom_event = (Eom_Event *)user_data;
   tdm_error err = TDM_ERROR_NONE;
   tbm_surface_h src_buffer;
   tbm_surface_h dst_buffer;

   EOM_ERR("Event\n");

   src_buffer = _e_eom_root_window_tdm_surface_get();
   if (!src_buffer )
     {
       EOM_ERR("Event: tdm_buffer is NULL\n");
       return;
     }

   dst_buffer = eom_event->dst_buffers;

   /* Do clone */
   tdm_layer_set_buffer(eom_event->layer, dst_buffer);

   err = tdm_output_commit(eom_event->output, 0, NULL, eom_event);
   if (err != TDM_ERROR_NONE)
     {
       EOM_ERR("Event: Cannot commit crtc\n");
       return;
     }
}

static tbm_surface_h
_e_eom_create_extrenal_output_buffer(int width, int height)
{
   tbm_surface_h buffer;
   tbm_surface_info_s buffer_info;

   /*
    * TODO: Add support of other formats
    */
   buffer = tbm_surface_internal_create_with_flags(width, height,
         TBM_FORMAT_ARGB8888, TBM_BO_SCANOUT);
   if (!buffer)
     {
      EOM_DBG("can not create dst_buffer\n");
      return NULL;
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
      return NULL;
   }

   memset(buffer_info.planes[0].ptr, 0xff, buffer_info.planes[0].size);
   tbm_surface_unmap(buffer);

   return buffer;
}

static void
_e_eom_put_src_to_dst( tbm_surface_h src_buffer, tbm_surface_h dst_buffer)
{



}

static Eina_Bool
_e_eom_ecore_drm_output_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Drm_Event_Output *e;
   E_EomPtr eom = data;
   E_Comp_Wl_Output *wl_output;
   tdm_output *hal_output;
   tdm_layer *hal_layer;
   tdm_info_layer layer_info;
   tbm_surface_h src_buffer;
   tbm_surface_h dst_buffer;
   tbm_surface_info_s src_buffer_info ;
   Eina_List *l2;
   Eom_Event *eom_event = &g_eom_event;
   struct wl_resource *output_resource;
   enum wl_eom_type eom_type = WL_EOM_TYPE_NONE;
   char buff[PATH_MAX];
   tdm_error tdm_err = TDM_ERROR_NONE;
   int tbm_err = 0;

   if (!(e = event)) goto end;

   if (!e->plug) goto end;

   EOM_DBG("id:%d (x,y,w,h):(%d,%d,%d,%d) (w_mm,h_mm):(%d,%d) refresh:%d subpixel_order:%d transform:%d make:%s model:%s name:%s plug:%d\n",
         e->id, e->x, e->y, e->w, e->h, e->phys_width, e->phys_height, e->refresh, e->subpixel_order, e->transform, e->make, e->model, e->name, e->plug);

   snprintf(buff, sizeof(buff), "%s", e->name);

   /* get the e_comp_wl_output */
   wl_output = _e_eom_e_comp_wl_output_get(e_comp_wl->outputs, buff);
   if (!wl_output)
     {
       EOM_ERR("no e_comp_wl_outputs. (%s)\n", buff);
       goto end;
     }

   /* Get hal output */
   hal_output = _e_eom_hal_output_get(buff, e->id);
   if (!hal_output)
     {
       EOM_ERR("no hal outputs, (%s)\n", buff);
       goto end;
     }

   hal_layer = _e_eom_hal_layer_get(hal_output, e->w, e->h);
   if (!hal_layer)
     {
       EOM_ERR("no hal layer\n");
       goto end;
     }

   /* Get main frame buffer */
   src_buffer = _e_eom_root_window_tdm_surface_get();
   if (!src_buffer )
     {
       EOM_ERR("no framebuffer\n");
       goto end;
     }

   dst_buffer = _e_eom_create_extrenal_output_buffer(e->w, e->h);
   if (!dst_buffer )
     {
       EOM_ERR("no framebuffer\n");
       goto end;
     }

   tbm_surface_get_info(src_buffer, &src_buffer_info );

   EOM_DBG("FRAMEBUFFER buffer: %dx%d   bpp:%d   size:%d",
         src_buffer_info.width,
         src_buffer_info.height,
         src_buffer_info.bpp,
         src_buffer_info.size);

   _e_eom_put_src_to_dst(src_buffer, dst_buffer);


   tdm_err = tdm_layer_get_info(hal_layer, &layer_info);
   if (tdm_err != TDM_ERROR_NONE)
     {
       EOM_ERR ("failed get layer info", tdm_err);
       goto end;
     }

   EOM_DBG("LAYER INFO: %dx%d, pos (x:%d, y:%d, w:%d, h:%d,  dpos (x:%d, y:%d, w:%d, h:%d))",
         layer_info.src_config.size.h,  layer_info.src_config.size.v,
         layer_info.src_config.pos.x, layer_info.src_config.pos.y,
         layer_info.src_config.pos.w, layer_info.src_config.pos.h,
         layer_info.dst_pos.x, layer_info.dst_pos.y,
         layer_info.dst_pos.w, layer_info.dst_pos.h);

   tdm_err = tdm_layer_set_buffer(hal_layer, dst_buffer);
   if (tdm_err != TDM_ERROR_NONE)
     {
       EOM_ERR("Failed set buffer on layer:%d\n", tdm_err);
       goto end;
     }

   eom_event->layer = hal_layer;
   eom_event->output = hal_output;
   eom_event->dst_buffers = dst_buffer;

   tdm_err = tdm_output_commit(hal_output, 0, _e_eom_output_cb_commit, &eom_event);
   if (tdm_err != TDM_ERROR_NONE)
     {
       EOM_ERR("Cannot commit crtc:%d\n", tdm_err);
       goto end;
     }

   EINA_LIST_FOREACH(wl_output->resources, l2, output_resource)
     {
       if (e->plug)
         {

           wl_eom_send_output_type(eom->resource,
                          output_resource,
                          eom_type,
                          WL_EOM_STATUS_CONNECTION);
         }
       else
         {
          EOM_DBG("7\n");

         wl_eom_send_output_type(eom->resource,
                          output_resource,
                          eom_type,
                          WL_EOM_STATUS_DISCONNECTION);
         }
     }

   EOM_DBG("8\n");

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
   Eina_List *l;

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
   E_Pixmap *pixmap = ec->pixmap;
   E_Comp_Wl_Buffer *buffer = e_pixmap_resource_get(pixmap);
   tbm_surface_h tsurface = NULL;
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *) e_comp->wl_comp_data;

   EINA_SAFETY_ON_NULL_RETURN_VAL(buffer != NULL, NULL);

   tsurface = wayland_tbm_server_get_surface(wl_comp_data->tbm.server, buffer->resource);

   return tsurface;
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
   Ecore_Drm_Output * drm_output;
   tbm_surface_h surface;

   EINA_SAFETY_ON_NULL_RETURN(ec != NULL);
   EINA_SAFETY_ON_NULL_RETURN(ec->frame != NULL);

   drm_output = _e_eom_get_drm_output_for_client(ec);
   EINA_SAFETY_ON_NULL_RETURN(drm_output != NULL);

   surface = _e_eom_get_tbm_surface_for_client(ec);

   _e_eom_set_output(drm_output, surface);

   evas_event_callback_add(ec->frame, EVAS_CALLBACK_RENDER_POST, _e_eom_canvas_render_post, ec);
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
_e_eom_wl_request_set_attribute_cb(struct wl_client *client, struct wl_resource *resource, struct wl_resource *output, uint32_t attribute)
{
   (void) client;
   (void) attribute;

   EOM_DBG("attribute:%d\n", attribute);

   wl_eom_send_output_attribute(resource,
                        output,
                        attribute,
                        WL_EOM_ATTRIBUTE_STATE_ACTIVE,
                        WL_EOM_ERROR_NONE);
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
   E_EomPtr eom = data;
   struct wl_resource *resource;

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

   EOM_DBG("1\n");

   g_eom->dpy = tdm_display_init(&err);
   if (err != TDM_ERROR_NONE)
     {
      EOM_DBG("failed initialize TDM\n");
      goto err;
     }

   EOM_DBG("2\n");

   err = tdm_display_get_fd(g_eom->dpy, &g_eom->fd);
   if (err != TDM_ERROR_NONE)
     {
       EOM_DBG("failed get FD\n");
       goto err;
     }

   EOM_DBG("3\n");

   g_eom->bufmgr = tbm_bufmgr_init(g_eom->fd);
   if (!g_eom->bufmgr)
     {
       EOM_DBG("failed initialize buffer manager\n");
       goto err;
     }

   EOM_DBG("4\n");

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
