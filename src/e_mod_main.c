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
} Eom_Event;

static Eom_Event g_eom_event;

E_EomPtr g_eom = NULL;

E_API E_Module_Api e_modapi = { E_MODULE_API_VERSION, "EOM Module" };

static E_Client_Hook *fullscreen_pre_hook = NULL;


static E_Comp_Wl_Output *
_e_eom_e_comp_wl_output_get(Eina_List *outputs, const char *id)
{
   Eina_List *l;
   E_Comp_Wl_Output *output, *out;
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

static Ecore_Drm_Hal_Output *
_e_eom_e_comp_hal_output_get(const char *id, int primary_output_id)
{
   Ecore_Drm_Hal_Output * hal_output;
   Ecore_Drm_Output *drm_output;
   Ecore_Drm_Device *dev;
   Eina_List *l;


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

   hal_output =  ecore_drm_output_hal_private_get(drm_output);
   if (!hal_output)
     {
       EOM_DBG("not find hal output output\n");
       return NULL;
     }

   EOM_DBG("find\n");
   return hal_output;
}

static tbm_surface_h
_e_eom_e_comp_tdm_surface_get()
{
   Evas_Engine_Info_Drm *einfo;
   Ecore_Drm_Fb* fb;
   Ecore_Evas *ee;
   Evas *evas;

   EOM_DBG("1\n");

   if (!e_comp || !e_comp->evas /*|| e_comp->ee->evas*/)
    return NULL;

   EOM_DBG("2\n");

   einfo = (Evas_Engine_Info_Drm *)evas_engine_info_get(e_comp->evas);

   EOM_DBG("3\n");

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
_ecore_drm_display_output_cb_commit(tdm_output *output EINA_UNUSED, unsigned int sequence EINA_UNUSED,
                           unsigned int tv_sec EINA_UNUSED, unsigned int tv_usec EINA_UNUSED,
                           void *user_data)
{
   Eom_Event *eom_event = (Eom_Event *)user_data;
   tdm_error err = TDM_ERROR_NONE;

   tbm_surface_h tdm_buffer = _e_eom_e_comp_tdm_surface_get();
   if (!tdm_buffer)
     {
       EOM_ERR("Event: tdm_buffer is NULL\n");
       return;
     }

   /* Do clone */
   tdm_layer_set_buffer(eom_event->layer, tdm_buffer);

   err = tdm_output_commit(eom_event->output, 0, NULL, eom_event);
   if (err != TDM_ERROR_NONE)
     {
       EOM_ERR("Event: Cannot commit crtc\n");
       return;
     }
}

static Eina_Bool
_e_eom_ecore_drm_output_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Drm_Event_Output *e;
   E_EomPtr eom = data;
   E_Comp_Wl_Output *wl_output;
   Ecore_Drm_Hal_Output *hal_output;
   tbm_surface_h tdm_buffer;
   tbm_surface_info_s tdm_buffer_info;
   Ecore_Drm_Fb *fb;
   Ecore_Drm_Device *dev;
   Eina_List *l, *l2;
   Eom_Event *eom_event = &g_eom_event;
   struct wl_resource *output_resource;
   enum wl_eom_type eom_type = WL_EOM_TYPE_NONE;
   char buff[PATH_MAX];
   tdm_error err = TDM_ERROR_NONE;

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
   hal_output = _e_eom_e_comp_hal_output_get(buff, e->id);
   if (!hal_output)
     {
       EOM_ERR("no hal outputs, (%s)\n", buff);
       goto end;
     }

   /* Get main frame buffer */
   tdm_buffer = _e_eom_e_comp_tdm_surface_get();
   if (!tdm_buffer )
     {
       EOM_ERR("no framebuffer\n");
       goto end;
     }

   tbm_surface_get_info(tdm_buffer, tdm_buffer_info );

   EOM_DBG("%dx%d   bpp:%d   size:%d", tdm_buffer_info->width,
         tdm_buffer_info->height, tdm_buffer_info->bpp, tdm_buffer_info->size);

   /*
    *  TODO: convert primary output size to external one
    */

   /* Do clone */
   tdm_layer_set_buffer(hal_output->primary_layer, tdm_buffer);

   eom_event->layer = hal_output->primary_layer;
   eom_event->output = hal_output->output;

   err = tdm_output_commit(hal_output->output, 0, _ecore_drm_display_output_cb_commit, &eom_event);
   if (err != TDM_ERROR_NONE)
     {
       EOM_ERR("Cannot commit crtc\n");
       return ECORE_CALLBACK_PASS_ON;
     }


   EINA_LIST_FOREACH(wl_output->resources, l2, output_resource)
     {
       if (e->plug)
         wl_eom_send_output_type(eom->resource,
                          output_resource,
                          eom_type,
                          WL_EOM_STATUS_CONNECTION);
       else
         wl_eom_send_output_type(eom->resource,
                          output_resource,
                          eom_type,
                          WL_EOM_STATUS_DISCONNECTION);
     }

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

static Eina_Bool
_e_eom_init()
{
   EINA_SAFETY_ON_NULL_GOTO(e_comp_wl, err);

   g_eom = E_NEW(E_Eom, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(g_eom, EINA_FALSE);

   g_eom->global = wl_global_create(e_comp_wl->wl.disp,
                           &wl_eom_interface,
                           1,
                           g_eom,
                           _e_eom_wl_bind_cb);
   EINA_SAFETY_ON_NULL_GOTO(g_eom->global, err);

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
