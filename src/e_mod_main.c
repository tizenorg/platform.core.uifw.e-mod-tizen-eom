#define E_COMP_WL
#include "e.h"
#include "e_mod_main.h"
#include "eom-server-protocol.h"
#include "Ecore_Drm.h"

typedef struct _E_Eom E_Eom, *E_EomPtr;

struct _E_Eom
{
   E_Comp_Data *cdata;
   struct wl_global *global;
   struct wl_resource *resource;
   Eina_List *handlers;
};

E_EomPtr g_eom = NULL;

EAPI E_Module_Api e_modapi = { E_MODULE_API_VERSION, "EOM Module" };

static E_Comp_Wl_Output *
_e_eom_e_comp_wl_output_get(Eina_List *outputs, const char *id)
{
   Eina_List *l;
   E_Comp_Wl_Output *output;

   EINA_LIST_FOREACH(outputs, l, output)
     {
       if (!strcmp(output->id, id))
         return output;
     }

   return NULL;
}

static Eina_Bool
_e_eom_e_client_remove_cb(void *data, int type, void *event)
{
   E_Event_Client *ev = event;
   E_Client *ec = ev->ec;

   (void) type;
   (void) event;
   (void) data;

   EOM_DBG("e_client: %p is died\n", ec);

   return ECORE_CALLBACK_PASS_ON;
}


static Eina_Bool
_e_eom_ecore_drm_output_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Drm_Event_Output *e;
   E_EomPtr eom = data;
   E_Comp_Data *cdata;
   E_Comp_Wl_Output *output;
   Eina_List *l2;
   struct wl_resource *output_resource;
   enum wl_eom_type eom_type = WL_EOM_TYPE_NONE;
   char buff[PATH_MAX];

   if (!(e = event)) goto end;

   if (!e->plug) goto end;

   EOM_DBG("id:%d (x,y,w,h):(%d,%d,%d,%d) (w_mm,h_mm):(%d,%d) refresh:%d subpixel_order:%d transform:%d make:%s model:%s plug:%d\n",
      e->id, e->x, e->y, e->w, e->h, e->phys_width, e->phys_height, e->refresh, e->subpixel_order, e->transform, e->make, e->model, e->plug);

   if (!(cdata = e_comp->wl_comp_data)) goto end;

   snprintf(buff, sizeof(buff), "%d", e->id);

   /* get the e_comp_wl_output */
   output = _e_eom_e_comp_wl_output_get(cdata->outputs, buff);
   if (!output)
     {
        EOM_ERR("no e_comp_wl_outputs.\n");
        goto end;
     }

   /* TODO: we need ecore_drm_output_connector_get()/ecore_drm_output_conn_name_get() function to get the connector type */


   /* send notify in each outputs associated with e_comp_wl_output */
   EINA_LIST_FOREACH(output->resources, l2, output_resource)
     {
        if (e->plug)
           wl_eom_send_output_type(eom->resource,  output_resource, eom_type, WL_EOM_STATUS_CONNECTION);
        else
            wl_eom_send_output_type(eom->resource,  output_resource, eom_type, WL_EOM_STATUS_DISCONNECTION);
     }

#if 0
   e_comp_wl_output_init(buff, e->make, e->model, e->x, e->y, e->w, e->h,
                         e->phys_width, e->phys_height, e->refresh,
                         e->subpixel_order, e->transform);
#endif

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_eom_ecore_drm_activate_cb(void *data, int type EINA_UNUSED, void *event)
{
   Ecore_Drm_Event_Activate *e = NULL;
   E_EomPtr eom = NULL;

   if ((!event) || (!data)) goto end;
   e = event;
   data = eom;

   if (e->active)
     {
        /* TODO: something do */
     }
   else
     {
        /* TODO: something do */
     }

end:
   return ECORE_CALLBACK_PASS_ON;
}


/* wl_eom_set_keygrab request handler */
static void
_e_eom_wl_request_set_attribute_cb(struct wl_client *client,
              struct wl_resource *resource,
              struct wl_resource *output,
              uint32_t attribute)
{
   (void) client;
   (void) attribute;

   EOM_DBG("attribute:%d\n", attribute);

   wl_eom_send_output_attribute(resource, output, attribute, WL_EOM_ATTRIBUTE_STATE_ACTIVE, WL_EOM_ERROR_NONE);
}

static const struct wl_eom_interface _e_eom_wl_implementation = {
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

   resource = wl_resource_create(client, &wl_eom_interface, MIN(version, 1), id);
   if (!resource)
     {
        EOM_ERR("error. resource is null. (version :%d, id:%d)\n", version, id);
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(resource, &_e_eom_wl_implementation, eom, _e_eom_wl_resource_destory_cb);

   eom->resource = resource;

   EOM_DBG("create wl_eom global resource.\n");
}

static void
_e_eom_deinit()
{
    Ecore_Event_Handler *h = NULL;

   if (!g_eom) return;

   /* remove event handlers */
   if (g_eom->handlers)
     {
        EINA_LIST_FREE(g_eom->handlers, h)
          ecore_event_handler_del(h);
     }

   if (g_eom->global) wl_global_destroy(g_eom->global);

   E_FREE(g_eom);
   g_eom = NULL;
}

static Eina_Bool
_e_eom_init()
{
   E_Comp_Data *cdata = NULL;

   g_eom = E_NEW(E_Eom, 1);
   if (!g_eom)
     {
        EOM_ERR("error. fail to allocate the memory.\n");
        return EINA_FALSE;
     }

   if (!e_comp)
     {
        EOM_ERR("error. e_comp is null.\n");
        goto err;
     }

   cdata = e_comp->wl_comp_data;
   if (!cdata)
     {
        EOM_ERR("error. e_comp->wl_comp_data is null.\n");
        goto err;
     }

   g_eom->cdata = cdata;
   g_eom->global = wl_global_create(cdata->wl.disp, &wl_eom_interface, 1, g_eom, _e_eom_wl_bind_cb);
   if (!g_eom->global)
     {
        EOM_ERR("error. g_eom->global is null.\n");
        goto err;
     }

   /* add event hanlders */
   E_LIST_HANDLER_APPEND(g_eom->handlers, ECORE_DRM_EVENT_ACTIVATE, _e_eom_ecore_drm_activate_cb, g_eom);
   E_LIST_HANDLER_APPEND(g_eom->handlers, ECORE_DRM_EVENT_OUTPUT, _e_eom_ecore_drm_output_cb, g_eom);
   E_LIST_HANDLER_APPEND(g_eom->handlers, E_EVENT_CLIENT_REMOVE, _e_eom_e_client_remove_cb, g_eom);

   return EINA_TRUE;

err:
   _e_eom_deinit();

   return EINA_FALSE;
}

EAPI void *
e_modapi_init(E_Module *m)
{
   return (_e_eom_init() ? m : NULL);
}

EAPI int
e_modapi_shutdown(E_Module *m EINA_UNUSED)
{
   _e_eom_deinit();

   return 1;
}

EAPI int
e_modapi_save(E_Module *m EINA_UNUSED)
{
   /* Save something to be kept */
   return 1;
}

