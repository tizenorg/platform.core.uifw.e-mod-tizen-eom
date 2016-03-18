#define E_COMP_WL
#include "e.h"
#include "e_mod_main.h"
#include "eom-server-protocol.h"
#include "Ecore_Drm.h"
#include <tdm.h>

typedef struct _E_Eom E_Eom, *E_EomPtr;

struct _E_Eom
{
   struct wl_global *global;
   struct wl_resource *resource;
   Eina_List *handlers;

   tdm_display *dpy;
   tdm_layer *layer;
   tbm_bufmgr bufmgr;
   tbm_surface_h surface;

   int fd;
};

typedef struct _Ecore_Drm_Hal_Output
{
   tdm_output *output;
   tdm_layer *primary_layer;
} Ecore_Drm_Hal_Output;

E_EomPtr g_eom = NULL;

E_API E_Module_Api e_modapi = { E_MODULE_API_VERSION, "EOM Module" };

static Ecore_Drm_Output *
_e_eom_e_comp_wl_output_get(Eina_List *outputs, const char *id)
{
   Ecore_Drm_Output *drm_output;
   Ecore_Drm_Device *dev;
   Eina_List *l;

   /*
   if (strcmp(id, "HDMI-A-0"))
   {
	   EOM_DBG("not find output\n");
	   return NULL;
   }
   */


   EINA_LIST_FOREACH(ecore_drm_devices_get(), l, dev)
   {
	   drm_output = ecore_drm_device_output_name_find(dev, id);
   }

   if (!drm_output)
   {
	   EOM_DBG("not find drm output\n");
	   return NULL;
   }

   EOM_DBG("find\n");
   return drm_output;

}

static Ecore_Drm_Output_Mode *
_e_eom_get_best_mode(Ecore_Drm_Output *output)
{
   Ecore_Drm_Output_Mode *mode, *m = NULL;
   const Eina_List *l;
   int w = 0, h = 0;

   /*
    * Get the biggest mode we can find
    */
   EINA_LIST_FOREACH(ecore_drm_output_modes_get(output), l, m)
   {
	   if (m->width + m->height >= w+h)
	   {
		   w = m->width;
		   h = m->height;

		   mode = m;
	   }
   }

   if (!mode)
	   return NULL;

   EOM_DBG("best mode: %dx%d\n", mode->width, mode->height);
   return mode;
}

static void
_print_tdm_transform_info(tdm_transform transform)
{
	if (transform == TDM_TRANSFORM_NORMAL)
		EOM_DBG("TDM_TRANSFORM_NORMAL\n");
	else if (transform == TDM_TRANSFORM_90)
		EOM_DBG("TDM_TRANSFORM_90\n");
	else if (transform == TDM_TRANSFORM_180)
		EOM_DBG("TDM_TRANSFORM_180\n");
	else if (transform == TDM_TRANSFORM_270)
		EOM_DBG("TDM_TRANSFORM_270\n");
	else if (transform == TDM_TRANSFORM_FLIPPED)
		EOM_DBG("TDM_TRANSFORM_FLIPPED\n");
	else if (transform == TDM_TRANSFORM_FLIPPED_90)
		EOM_DBG("TDM_TRANSFORM_FLIPPED_90\n");
	else if (transform == TDM_TRANSFORM_FLIPPED_180)
		EOM_DBG("TDM_TRANSFORM_FLIPPED_180\n");
	else if (transform == TDM_TRANSFORM_FLIPPED_270)
		EOM_DBG("TDM_TRANSFORM_FLIPPED_270\n");
	else
		EOM_DBG("TDM_TRANSFORM info error\n");
}

static void
_get_layer(Ecore_Drm_Output *output, Ecore_Drm_Output_Mode *mode)
{
	Ecore_Drm_Hal_Output * hal_output;
	tdm_layer *layer = NULL;
	tdm_info_layer layer_info;
	tdm_info_layer set_layer_info;
	tdm_layer_capability capa;
	tdm_error err = TDM_ERROR_NONE;
	int count, i;

	count = 0;

	hal_output = ecore_drm_output_hal_private_get(output);

	err = tdm_output_get_layer_count( hal_output->output, &count);
	if (err != TDM_ERROR_NONE) {
		EOM_DBG ("\ttdm_output_get_layer_count fail(%d)\n", err);
		return ;
	}
	EOM_DBG ("\ttdm output layer's count:%d\n", count);

	for (i = 0; i < count; i++) {
		layer = (tdm_layer *)tdm_output_get_layer( hal_output->output, i, &err);
		if (err != TDM_ERROR_NONE) {
			EOM_DBG ("\ttdm_output_get_layer fail(%d)\n", err);
			return ;
		}

		err = tdm_layer_get_capabilities(layer, &capa);
		if (err != TDM_ERROR_NONE) {
			EOM_DBG ("\ttdm_layer_get_capabilities fail(%d)\n", err);
			return ;
		}

		if (capa & TDM_LAYER_CAPABILITY_PRIMARY) {
			EOM_DBG ("\tTDM_LAYER_CAPABILITY_PRIMARY layer found : %d\n", i);
			g_eom->layer = layer;
			break;
		}
	}

	if (i == count || !layer) {
		EOM_DBG ("\tTDM_LAYER_CAPABILITY_PRIMARY layer find fail(%d)\n", err);
		return ;
	}

	memset(&layer_info, 0x0, sizeof(tdm_info_layer));
	set_layer_info.src_config.size.h = mode->width;
	set_layer_info.src_config.size.v = mode->height;
	set_layer_info.src_config.pos.x = 0;
	set_layer_info.src_config.pos.y = 0;
	set_layer_info.src_config.pos.w = mode->width;
	set_layer_info.src_config.pos.h = mode->height;
	set_layer_info.src_config.format = TBM_FORMAT_ARGB8888;
	set_layer_info.dst_pos.x = 0;
	set_layer_info.dst_pos.y = 0;
	set_layer_info.dst_pos.w = mode->width;
	set_layer_info.dst_pos.h = mode->height;
	set_layer_info.transform = TDM_TRANSFORM_NORMAL;
	err = tdm_layer_set_info(layer, &set_layer_info);
	if (err != TDM_ERROR_NONE) {
		EOM_DBG ("\ttdm_layer_set_info fail(%d)\n", err);
		return ;
	}
	EOM_DBG ("\ttdm_layer_set_info success\n");

	err = tdm_layer_get_info(g_eom->layer, &layer_info);
	if (err != TDM_ERROR_NONE) {
		EOM_DBG ("\ttdm_layer_get_info 2 fail(%d)\n", err);
		return ;
	}
	EOM_DBG("\t*** layer info ***\n");
	EOM_DBG("\tsrc_config\n");
	EOM_DBG("\t\tsize (h:%d, v:%d), ", layer_info.src_config.size.h,
		   layer_info.src_config.size.v);
	EOM_DBG("pos (x:%d, y:%d, w:%d, h:%d), ",
		   layer_info.src_config.pos.x, layer_info.src_config.pos.y,
		   layer_info.src_config.pos.w, layer_info.src_config.pos.h);
	EOM_DBG("\tdst_pos\n");
	EOM_DBG("\t\tpos (x:%d, y:%d, w:%d, h:%d)\n",
		   layer_info.dst_pos.x, layer_info.dst_pos.y, layer_info.dst_pos.w,
		   layer_info.dst_pos.h);
	EOM_DBG("\ttransform\n\t\t");
	_print_tdm_transform_info(layer_info.transform);
	EOM_DBG("\n");
}

static void
_create_buffer(Ecore_Drm_Output_Mode *mode)
{
	tbm_surface_h surface;
	tbm_surface_info_s surface_info;
	tdm_error err = TDM_ERROR_NONE;
	int fd;

	err = tdm_display_get_fd(g_eom->dpy, &fd);
	if (err != TDM_ERROR_NONE) {
		EOM_DBG("\ttdm_display_get_fd fail(%d)\n", err);
		return;
	}
	g_eom->fd = fd;

	g_eom->bufmgr = tbm_bufmgr_init(fd);
	if (!g_eom->bufmgr) {
		EOM_DBG("\ttbm_bufmgr_init fail\n");
		return;
	}

	/* create buffer 1 */
	surface = tbm_surface_internal_create_with_flags(mode->width, mode->height,
	                TBM_FORMAT_ARGB8888, TBM_BO_SCANOUT);
	if (surface == NULL) {
		EOM_DBG("\ttbm_surface_create fail\n");
		return ;
	}
	g_eom->surface = surface;

	memset(&surface_info, 0x0, sizeof(tbm_surface_info_s));
	if (tbm_surface_map(surface, TBM_SURF_OPTION_READ | TBM_SURF_OPTION_WRITE,
	                    &surface_info) != TBM_SURFACE_ERROR_NONE) {
		EOM_DBG("\ttbm_surface_map fail\n");
		return ;
	}
	EOM_DBG("\tsurface info after map\n");

	memset(surface_info.planes[0].ptr, 0xAA, surface_info.planes[0].size);

	tbm_surface_unmap(surface);
}

static void
tdm_primary_commit_handler(tdm_output *output, unsigned int sequence,
                           unsigned int tv_sec, unsigned int tv_usec, void *user_data)
{
	EOM_DBG("COMMIT_HANDLER\n");
}

static void
do_output(Ecore_Drm_Output *output, Ecore_Drm_Output_Mode *mode)
{
	tdm_error err = TDM_ERROR_NONE;

	_get_layer(output, mode);
	_create_buffer(mode);

	err = tdm_layer_set_buffer(g_eom->layer, g_eom->surface);
	if (err != TDM_ERROR_NONE) {
		EOM_DBG ("\ttdm_layer_set_buffer 1 fail(%d)\n", err);
		return;
	}

	err = tdm_output_commit(output, 0, tdm_primary_commit_handler,
							g_eom);
	if (err != TDM_ERROR_NONE) {
		EOM_DBG ("\ttdm_output_commit fail(%d)\n", err);
		return;
	}
}

static Eina_Bool
_e_eom_ecore_drm_output_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Drm_Event_Output *e;
   E_EomPtr eom = data;
   Ecore_Drm_Output *output;
   Ecore_Drm_Output_Mode *mode;
   Eina_List *l2;
   struct wl_resource *output_resource;
   enum wl_eom_type eom_type = WL_EOM_TYPE_NONE;
   char buff[PATH_MAX];
   tdm_error err = TDM_ERROR_NONE;


   if (!(e = event)) goto end;

   if (!e->plug) goto end;

   EOM_DBG("id:%d (x,y,w,h):(%d,%d,%d,%d) (w_mm,h_mm):(%d,%d) refresh:%d subpixel_order:%d transform:%d make:%s model:%s name:%s plug:%d\n",
           e->id, e->x, e->y, e->w, e->h, e->phys_width, e->phys_height, e->refresh, e->subpixel_order, e->transform, e->make, e->model, e->name, e->plug);

   snprintf(buff, sizeof(buff), "%s", e->name);

   /* get the Ecore_Drm_Output */

    output = _e_eom_e_comp_wl_output_get(e_comp_wl->outputs, buff);
	if (!output)
	{
		EOM_ERR("no Ecore_Drm_Output, (%s)\n", buff);
		goto end;
	}

   g_eom->dpy = tdm_display_init(&err);
   if (err != TDM_ERROR_NONE)
   {
	   EOM_ERR("Failed init TDM display\n");
	   goto end;
   }

	mode = _e_eom_get_best_mode(output);
    if (!mode)
	{
    	EOM_ERR("no Mode");
	    	goto end;
	}

    do_output(output, mode);


   /* TODO:
    * we need ecore_drm_output_connector_get()/ecore_drm_output_conn_name_get()
    * function to get the connector type
    */

   /*
   EINA_LIST_FOREACH(output->resources, l2, output_resource)
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
	*/
end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_eom_ecore_drm_activate_cb(void *data, int type EINA_UNUSED, void *event)
{
   Ecore_Drm_Event_Activate *e = NULL;
   E_EomPtr eom = NULL;

   EOM_DBG("_e_eom_ecore_drm_activate_cb called\n");

   if ((!event) || (!data)) goto end;
   e = event;
   eom = data;

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

   if (g_eom->global) wl_global_destroy(g_eom->global);

   E_FREE(g_eom);
}

static Eina_Bool
_e_eom_init()
{
   EINA_SAFETY_ON_NULL_GOTO(e_comp_wl, err);

   g_eom = E_NEW(E_Eom, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(g_eom, NULL);

   g_eom->global = wl_global_create(e_comp_wl->wl.disp,
                                    &wl_eom_interface,
                                    1,
                                    g_eom,
                                    _e_eom_wl_bind_cb);
   EINA_SAFETY_ON_NULL_GOTO(g_eom->global, err);

   E_LIST_HANDLER_APPEND(g_eom->handlers, ECORE_DRM_EVENT_ACTIVATE, _e_eom_ecore_drm_activate_cb, g_eom);
   E_LIST_HANDLER_APPEND(g_eom->handlers, ECORE_DRM_EVENT_OUTPUT,   _e_eom_ecore_drm_output_cb,   g_eom);

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
