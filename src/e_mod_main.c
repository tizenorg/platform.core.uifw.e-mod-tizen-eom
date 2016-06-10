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
static E_EomPtr g_eom = NULL;

static const struct wl_eom_interface _e_eom_wl_implementation =
{
   _e_eom_cb_wl_request_set_attribute,
   _e_eom_cb_wl_request_get_output_info
};

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

//////////////////////////////////////////////////////////////////////////////////////////////////////

static inline eom_output_mode_e
_e_eom_output_state_get_mode(E_EomOutputPtr output)
{
   if (output == NULL)
     return EOM_OUTPUT_MODE_NONE;
   return output->mode;
}

static inline void
_e_eom_output_state_set_mode(E_EomOutputPtr output, eom_output_mode_e mode)
{
   output->mode = mode;
}

static inline eom_output_attribute_e
_e_eom_output_state_get_attribute_state(E_EomOutputPtr output)
{
   if (output == NULL)
     return EOM_OUTPUT_ATTRIBUTE_STATE_NONE;
   return output->attribute_state;
}

static inline void
_e_eom_output_attribute_state_set(E_EomOutputPtr output, eom_output_attribute_e attribute_state)
{
   if (output == NULL)
     return;
   output->attribute_state = attribute_state;
}

static inline eom_output_attribute_e
_e_eom_output_state_get_attribute(E_EomOutputPtr output)
{
   if (output == NULL)
     return EOM_OUTPUT_ATTRIBUTE_NONE;
   return output->attribute;
}

static inline void
_e_eom_output_state_set_force_attribute(E_EomOutputPtr output, eom_output_attribute_e attribute)
{
   if (output == NULL)
     return;
   output->attribute = attribute;
}

static inline Eina_Bool
_e_eom_output_state_set_attribute(E_EomOutputPtr output, eom_output_attribute_e attribute)
{
   if (output == NULL)
     return EINA_FALSE;

   if (attribute == EOM_OUTPUT_ATTRIBUTE_NONE || output->attribute == EOM_OUTPUT_ATTRIBUTE_NONE)
     {
        output->attribute = attribute;
        return EINA_TRUE;
     }

   if (eom_output_attributes[output->attribute - 1][attribute - 1] == 1)
     {
        output->attribute = attribute;
        return EINA_TRUE;
     }

   return EINA_FALSE;
}

static inline tdm_output_conn_status
_e_eom_output_state_get_status(E_EomOutputPtr output)
{
   if (output == NULL)
     return TDM_OUTPUT_CONN_STATUS_DISCONNECTED;
   return output->status;
}

static inline void
_e_eom_output_state_set_status(E_EomOutputPtr output, tdm_output_conn_status status)
{
   if (output == NULL)
     return;
   output->status = status;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

static void
_e_eom_cb_wl_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *resource = NULL;
   E_EomClientPtr new_client = NULL;

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
                                  _e_eom_cb_wl_resource_destory);

   EOM_DBG("send - output count : %d\n", g_eom->output_count);

   wl_eom_send_output_count(resource, g_eom->output_count);

   if (g_eom->outputs)
     {
        Eina_List *l;
        E_EomOutputPtr output = NULL;

        EINA_LIST_FOREACH(g_eom->outputs, l, output)
          {
             EOM_DBG("send - id : %d, type : %d, mode : %d, w : %d, h : %d, w_mm : %d, h_mm : %d, conn : %d\n",
                     output->id, output->type, output->mode, output->width, output->height,
                     output->phys_width, output->phys_height, output->status);
             wl_eom_send_output_info(resource, output->id, output->type, output->mode, output->width, output->height,
                                     output->phys_width, output->phys_height, output->status);

             wl_eom_send_output_attribute(resource,
                                          output->id,
                                          _e_eom_output_state_get_attribute(output),
                                          _e_eom_output_state_get_attribute_state(output),
                                          EOM_ERROR_NONE);
          }
     }

   new_client = E_NEW(E_EomClient, 1);
   if (new_client == NULL)
     {
        EOM_ERR("allocate new client");
        /*TODO: should resource be deleted?*/
        return;
     }

   new_client->resource = resource;
   new_client->current = EINA_FALSE;
   new_client->output_id = -1;
   new_client->buffers = NULL;

   g_eom->clients = eina_list_append(g_eom->clients, new_client);
}

static void
_e_eom_cb_wl_resource_destory(struct wl_resource *resource)
{
   E_EomClientPtr client = NULL, iterator = NULL;
   E_EomOutputPtr output = NULL;
   Eina_List *l = NULL;
   Eina_Bool ret;

   EOM_DBG("client unbind\n");

   client = _e_eom_client_get_by_resource(resource);
   RETURNIFTRUE(client == NULL, "destroy client: client is NULL");

   g_eom->clients = eina_list_remove(g_eom->clients, client);

   /* If it is not current client do nothing */
   GOTOIFTRUE(client->current == EINA_FALSE, end2, "");

   output = _e_eom_output_get_by_id(client->output_id);
   GOTOIFTRUE(output == NULL, end2, "destroy client: client is NULL");

   _e_eom_client_free_buffers(client);

   _e_eom_output_state_set_mode(output, EOM_OUTPUT_MODE_MIRROR);
   ret = _e_eom_output_state_set_attribute(output, EOM_OUTPUT_ATTRIBUTE_NONE);
   (void)ret;

   /* If a client has been disconnected and mirror mode has not
    * been restore, start mirror mode
    */
   if (output->mirror_run == DOWN)
     {
        output->mirror_run = UP;
        ret = _e_eom_output_start_pp(output);
        GOTOIFTRUE(ret == EINA_FALSE, end,
                   "ERROR: restore mirror mode after a client disconnection\n");
     }

end:
   /* Notify eom clients which are binded to a concrete output that the
    * state and mode of the output has been changed */
   EINA_LIST_FOREACH(g_eom->clients, l, iterator)
     {
        if (iterator && iterator->output_id == output->id)
          {
             wl_eom_send_output_attribute(iterator->resource,
                                          output->id,
                                          _e_eom_output_state_get_attribute(output),
                                          _e_eom_output_state_get_attribute_state(output),
                                          EOM_OUTPUT_MODE_NONE);

             wl_eom_send_output_mode(iterator->resource,
                                     output->id,
                                     _e_eom_output_state_get_mode(output));
          }
     }

end2:
   free(client);
}

static Eina_Bool
_e_eom_cb_ecore_drm_output(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
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
              g_eom->width = e->w;
              g_eom->height = e->h;
              if (g_eom->int_output_name == NULL)
                g_eom->int_output_name = strdup(buff);

              g_eom->eom_state = UP;
          }
        else
          {
             g_eom->width= -1;
             g_eom->height = -1;
             if (g_eom->int_output_name)
               free(g_eom->int_output_name);

             g_eom->eom_state = DOWN;
          }
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_eom_cb_ecore_drm_activate(void *data, int type EINA_UNUSED, void *event)
{
 /*
   Ecore_Drm_Event_Activate *e = NULL;
   E_EomPtr eom = NULL;

   EOM_DBG("_e_eom_cb_ecore_drm_activate called\n");

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

static Eina_Bool
_e_eom_cb_client_buffer_change(void *data, int type, void *event)
{
   E_Comp_Wl_Buffer *external_wl_buffer = NULL;
   E_EomClientBufferPtr client_buffer = NULL;
   E_EomClientPtr client = NULL;
   E_EomOutputPtr output = NULL;
   E_Event_Client *ev = event;
   E_Client *ec = NULL;
   Eina_Bool ret_err;
   const char *output_name = NULL;
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

   /* We are not interested in non external clients */
   ret_err = e_client_is_external_output_client(ec);
   RETURNVALIFTRUE(ret_err != EINA_TRUE,
                   ECORE_CALLBACK_PASS_ON,
                   "ERROR: BUFF CHANGE: ec is not external\n");

   output_name = e_client_external_output_name_get(ec);
   RETURNVALIFTRUE(output_name == NULL,
                   ECORE_CALLBACK_PASS_ON,
                   "ERROR:BUFF CHANGE: ec is not bind to any external outputs\n");


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

   output = _e_eom_output_get_by_name(output_name);

   /* TODO: Must find proper way of getting tbm_surface */
   /*client_buffer = _e_eom_util_create_client_buffer(external_wl_buffer, external_tbm_buffer);*/
   client_buffer = _e_eom_util_create_client_buffer(external_wl_buffer, output->fake_buffer);
   RETURNVALIFTRUE(client_buffer == NULL,
                   ECORE_CALLBACK_PASS_ON,
                   "ERROR: BUFF CHANGE: alloc client buffer");

   /* TODO: What if not current client has committed a buffer */
   client = _e_eom_client_current_by_id_get(output->id);
   if (client == NULL)
     {
        EOM_DBG("BUFF CHANGE: current client is NULL");
        E_FREE(client_buffer);
        return ECORE_CALLBACK_PASS_ON;
     }

   _e_eom_client_add_buffer(client, client_buffer);

   /* Stop mirror mode */
   output->mirror_run = DOWN;

   return ECORE_CALLBACK_PASS_ON;

   /* TODO: Add deinitialization on error*/
}

static void
_e_eom_cb_pp(tbm_surface_h surface, void *user_data)
{
   tdm_error tdm_err = TDM_ERROR_NONE;
   E_EomOutputPtr eom_output = NULL;

   eom_output = (E_EomOutputPtr)user_data;
   RETURNIFTRUE(user_data == NULL, "ERROR: PP EVENT: user data is NULL");

   tdm_buffer_remove_release_handler(eom_output->dst_buffers[eom_output->pp_buffer],
                                     _e_eom_cb_pp, eom_output);

   /* TODO: lock these flags??? */
   if (g_eom->eom_state == DOWN)
     return;

   /* If a client has committed its buffer stop mirror mode */
   if (eom_output->mirror_run == DOWN)
     return;

   tbm_surface_h src_buffer;
   src_buffer = _e_eom_util_get_output_surface(g_eom->int_output_name);
   RETURNIFTRUE(src_buffer == NULL, "PP EVENT: get root tdm surface");

   /*TODO: rewrite the mirror mode buffer's switching */
   eom_output->pp_buffer ^= 1;
#ifdef EOM_SERVER_DBG
   EOM_DBG("PP: %d", eom_output->pp_buffer);
#endif

   tdm_err = tdm_buffer_add_release_handler(eom_output->dst_buffers[eom_output->pp_buffer],
                                            _e_eom_cb_pp, eom_output);
   RETURNIFTRUE(tdm_err != TDM_ERROR_NONE, "ERROR: PP EVENT: set pp hadler:%d", tdm_err );

   tdm_err = tdm_pp_attach(eom_output->pp, src_buffer, eom_output->dst_buffers[eom_output->pp_buffer]);
   RETURNIFTRUE(tdm_err != TDM_ERROR_NONE, "ERROR: pp attach:%d\n", tdm_err);

   tdm_err = tdm_pp_commit(eom_output->pp);
   RETURNIFTRUE(tdm_err != TDM_ERROR_NONE, "ERROR: PP EVENT: pp commit:%d", tdm_err );
}

static void
_e_eom_cb_commit(tdm_output *output EINA_UNUSED, unsigned int sequence EINA_UNUSED,
                           unsigned int tv_sec EINA_UNUSED, unsigned int tv_usec EINA_UNUSED,
                           void *user_data)
{
   E_EomClientBufferPtr client_buffer = NULL;
   E_EomOutputPtr eom_output = NULL;
   E_EomClientPtr eom_client = NULL;
   tdm_error err = TDM_ERROR_NONE;

   RETURNIFTRUE(user_data == NULL, "ERROR: PP EVENT: user data is NULL");

   eom_output = (E_EomOutputPtr)user_data;
   RETURNIFTRUE(user_data == NULL, "ERROR: PP EVENT: user data is NULL");

   if (g_eom->eom_state == DOWN)
     return;

   /* TODO: Maybe better to separating that callback on to mirror and extended callbacks */
   if (eom_output->mirror_run == UP)
     {
        /*TODO: rewrite the mirror mode buffer's switching */
        eom_output->current_buffer ^= 1;
        err = tdm_layer_set_buffer(eom_output->layer,
                                   eom_output->dst_buffers[eom_output->current_buffer]);

#ifdef EOM_SERVER_DBG
        EOM_DBG("COMMIT: MIRROR %d", eom_output->current_buffer);
#endif

        RETURNIFTRUE(err != TDM_ERROR_NONE, "ERROR: EVENT: set buffer 0 err:%d", err);

        err = tdm_output_commit(eom_output->output, 0, _e_eom_cb_commit, eom_output);
        RETURNIFTRUE(err != TDM_ERROR_NONE, "ERROR: EVENT commit");
     }
   else
     {
#ifdef EOM_SERVER_DBG
        EOM_DBG("COMMIT: FAKE");
#endif
        eom_client = _e_eom_client_current_by_id_get(eom_output->id);

        client_buffer = _e_eom_client_get_buffer(eom_client);
        RETURNIFTRUE(client_buffer == NULL, "ERROR: EVENT: client buffer is NULL");

        err = tdm_layer_set_buffer(eom_output->layer, client_buffer->tbm_buffer);
        RETURNIFTRUE(err != TDM_ERROR_NONE, "ERROR: EVENT: set buffer 1");

        err = tdm_output_commit(eom_output->output, 0, _e_eom_cb_commit, eom_output);
        RETURNIFTRUE(err != TDM_ERROR_NONE, "ERROR: EVENT: commit");
     }
}

static void
_e_eom_cb_tdm_output_status_change(tdm_output *output, tdm_output_change_type type, tdm_value value, void *user_data)
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
   E_EomClientPtr iterator = NULL;
   Eina_List *l;

   if (type == TDM_OUTPUT_CHANGE_DPMS || g_eom->eom_state == DOWN)
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
   /*TODO: What if there will more then one output of same type.
    *e.g. "HDMI and HDMI" "LVDS and LVDS"*/
   snprintf(new_name, sizeof(new_name), "%s-%d", tmp_name, 0);

   plug = value.u32;

   if (plug == TDM_OUTPUT_CONN_STATUS_CONNECTED || plug == TDM_OUTPUT_CONN_STATUS_MODE_SETTED)
     {
        unsigned int mmWidth, mmHeight, subpixel;
        const tdm_output_mode *mode;
        int x = 0;

        ret = tdm_output_get_physical_size(output, &mmWidth, &mmHeight);
        RETURNIFTRUE(ret != TDM_ERROR_NONE, "tdm_output_get_physical_size fail(%d)", ret);

        ret = tdm_output_get_subpixel(output, &subpixel);
        RETURNIFTRUE(ret != TDM_ERROR_NONE, "tdm_output_get_subpixel fail(%d)", ret);

        mode = _e_eom_output_get_best_mode(output);
        RETURNIFTRUE(mode == NULL, "_e_eom_get_best_resolution fail");

        ret = tdm_output_set_mode(output, mode);
        RETURNIFTRUE(ret != TDM_ERROR_NONE, "tdm_output_set_mode fail(%d)", ret);

        x = _e_eom_output_get_position();
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
        eom_output->width = mode->hdisplay;
        eom_output->height = mode->vdisplay;
        eom_output->phys_width = mmWidth;
        eom_output->phys_height = mmHeight;
        eom_output->status = plug;
        eom_output->name = eina_stringshare_add(new_name);
        eom_output->type = (eom_output_type_e)tdm_type;

        /* Create fake buffer */
        eom_output->fake_buffer = _e_eom_util_create_fake_buffer(eom_output->width, eom_output->height);

        /* TODO: check output mode(presentation set) and HDMI type */
        _e_eom_output_start_mirror(eom_output, mode->hdisplay, mode->vdisplay);

        /* If there were previously connected clients to the output - notify them */
        EINA_LIST_FOREACH(g_eom->clients, l, iterator)
          {
             if (iterator && iterator->output_id == eom_output->id)
               {
                  wl_eom_send_output_info(iterator->resource, eom_output->id,
                                          eom_output->type, eom_output->mode,
                                          eom_output->width, eom_output->height,
                                          eom_output->phys_width, eom_output->phys_height,
                                          eom_output->status);

                  wl_eom_send_output_attribute(iterator->resource,
                                               eom_output->id,
                                               _e_eom_output_state_get_attribute(eom_output),
                                               _e_eom_output_state_get_attribute_state(eom_output),
                                               EOM_ERROR_NONE);
               }
          }
     }
   else /*TDM_OUTPUT_CONN_STATUS_DISCONNECTED*/
     {
        if (eom_output->mirror_run == UP)
          _e_eom_output_stop_mirror(eom_output);

        /* update eom_output disconnect */
        eom_output->width = 0;
        eom_output->height = 0;
        eom_output->phys_width = 0;
        eom_output->phys_height = 0;
        eom_output->status = plug;

        /* If there were previously connected clients to the output - notify them */
        EINA_LIST_FOREACH(g_eom->clients, l, iterator)
          {
             if (iterator && iterator->output_id == eom_output->id)
               {
                  wl_eom_send_output_info(iterator->resource, eom_output->id,
                                          eom_output->type, eom_output->mode,
                                          eom_output->width, eom_output->height,
                                          eom_output->phys_width, eom_output->phys_height,
                                          eom_output->status);

                  wl_eom_send_output_attribute(iterator->resource,
                                               eom_output->id,
                                               _e_eom_output_state_get_attribute(eom_output),
											   EOM_OUTPUT_ATTRIBUTE_STATE_LOST,
                                               EOM_ERROR_NONE);
               }
          }

        if (eom_output->fake_buffer)
          tbm_surface_destroy(eom_output->fake_buffer);

        e_comp_wl_output_remove(new_name);
        EOM_DBG("Destory output: %s", new_name);
        eina_stringshare_del(eom_output->name);
     }
}

static void
_e_eom_cb_wl_request_set_attribute(struct wl_client *client, struct wl_resource *resource, uint32_t output_id, uint32_t attribute)
{
   eom_error_e eom_error = EOM_ERROR_NONE;
   E_EomClientPtr eom_client = NULL, iterator = NULL;
   E_EomOutputPtr eom_output = NULL;
   Eina_Bool changes = EINA_FALSE;
   Eina_Bool mode_change = EINA_FALSE;
   Eina_Bool ret = EINA_FALSE;
   Eina_List *l;

   eom_client = _e_eom_client_get_by_resource(resource);
   RETURNIFTRUE(eom_client == NULL, "client is NULL");

   /* Bind the client with a concrete output */
   if (eom_client->output_id  == -1)
     eom_client->output_id = output_id;

   eom_output = _e_eom_output_get_by_id(output_id);
   GOTOIFTRUE(eom_output == NULL, no_output, "output is NULL");

   if (eom_client->current == EINA_TRUE && eom_output->id == eom_client->output_id)
     {
        /* Current client can set any flag it wants */
        _e_eom_output_state_set_force_attribute(eom_output, attribute);
        changes = EINA_TRUE;
     }
   else
     {
        ret = _e_eom_output_state_set_attribute(eom_output, attribute);
        if (ret == EINA_FALSE)
          {
             EOM_DBG("set attribute FAILED\n");

             eom_error = EOM_ERROR_INVALID_PARAMETER;
             goto end;
          }

        eom_client->output_id = output_id;
        changes = EINA_TRUE;
     }

   EOM_DBG("set attribute OK\n");

   /* If client has set EOM_OUTPUT_ATTRIBUTE_NONE, eom will be
    * switched to mirror mode
    */
   if (attribute == EOM_OUTPUT_ATTRIBUTE_NONE && eom_output->mirror_run == DOWN)
     {
        eom_output->mirror_run= UP;

        _e_eom_output_state_set_mode(eom_output, EOM_OUTPUT_ATTRIBUTE_NONE);
        mode_change = EINA_TRUE;

        ret = _e_eom_output_state_set_attribute(eom_output, EOM_OUTPUT_ATTRIBUTE_NONE);
        (void)ret;

        _e_eom_client_free_buffers(eom_client);

        if (eom_output->status == 0)
          {
             EOM_DBG("ATTRIBUTE: output:%d is disconnected", output_id);
             goto end;
          }

        ret = _e_eom_output_start_pp(eom_output);
        GOTOIFTRUE(ret == EINA_FALSE, end,
                   "ERROR: restore mirror mode after a client disconnection\n");
        goto end;
     }

end:
   wl_eom_send_output_attribute(eom_client->resource,
                                eom_output->id,
                                _e_eom_output_state_get_attribute(eom_output),
                                _e_eom_output_state_get_attribute_state(eom_output),
                                eom_error);

   /* Notify eom clients that eom state has been changed */
   if (changes == EINA_TRUE)
     {
        EINA_LIST_FOREACH(g_eom->clients, l, iterator)
          {
             if (iterator && iterator->resource == resource)
               continue;

             if (iterator && iterator->output_id == eom_output->id)
               {
                 if (eom_output->mirror_run == UP)
                   wl_eom_send_output_attribute(iterator->resource,
                                                eom_output->id,
                                                _e_eom_output_state_get_attribute(eom_output),
                                                _e_eom_output_state_get_attribute_state(eom_output),
                                                EOM_ERROR_NONE);
                 else
                   wl_eom_send_output_attribute(iterator->resource,
                                                eom_output->id,
                                                _e_eom_output_state_get_attribute(eom_output),
                                                EOM_OUTPUT_ATTRIBUTE_STATE_LOST,
                                                EOM_ERROR_NONE);

                  if (mode_change == EINA_TRUE)
                    wl_eom_send_output_mode(iterator->resource,
                                            eom_output->id,
                                            _e_eom_output_state_get_mode(eom_output));
               }
          }

        eom_client->current= EINA_TRUE;
     }

   return;

   /* Get here if EOM does not have output with output_id is */
no_output:

   wl_eom_send_output_attribute(eom_client->resource,
                                output_id,
                                EOM_OUTPUT_ATTRIBUTE_NONE,
                                EOM_OUTPUT_ATTRIBUTE_STATE_NONE,
                                EOM_ERROR_NO_SUCH_DEVICE);

   wl_eom_send_output_mode(eom_client->resource,
                           output_id,
                           EOM_OUTPUT_MODE_NONE);

   wl_eom_send_output_type(eom_client->resource,
                           output_id,
                           EOM_OUTPUT_ATTRIBUTE_STATE_NONE,
                           TDM_OUTPUT_CONN_STATUS_DISCONNECTED);
   return;
}

static void
_e_eom_cb_wl_request_get_output_info(struct wl_client *client, struct wl_resource *resource, uint32_t output_id)
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
                          output->id, output->type, output->mode, output->width, output->height,
                          output->phys_width, output->phys_height, output->status);

                  wl_eom_send_output_info(resource, output->id, output->type, output->mode, output->width, output->height,
                                          output->phys_width, output->phys_height, output->status);
               }
          }
     }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

static Eina_Bool
_e_eom_output_init(tdm_display *dpy)
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

        ret = tdm_output_add_change_handler(output, _e_eom_cb_tdm_output_status_change, NULL);
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
             new_output->width = 0;
             new_output->height = 0;
          }

        else
          {
             new_output->width = mode->hdisplay;
             new_output->height = mode->vdisplay;
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
                new_output->width, new_output->height, new_output->phys_width, new_output->phys_height);

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

static const tdm_output_mode *
_e_eom_output_get_best_mode(tdm_output *output)
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

   for (i = 0; i < count; i++)
     {
        value = modes[i].vdisplay + modes[i].hdisplay;
        if (value >= best_value)
          {
             best_value = value;
             mode = &modes[i];
          }
     }

   EOM_DBG("bestmode : %s, (%dx%d) r(%d), f(%d), t(%d)",
           mode->name, mode->hdisplay, mode->vdisplay, mode->vrefresh, mode->flags, mode->type);

   return mode;
}

static int
_e_eom_output_get_position(void)
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
             if (eom_output_tmp->status == TDM_OUTPUT_CONN_STATUS_CONNECTED)
               x += eom_output_tmp->width;
          }
     }

   return x;
}

static void
_e_eom_output_start_mirror(E_EomOutputPtr eom_output, int width, int height)
{
   tdm_output *output;
   tdm_layer *hal_layer;
   tdm_info_layer layer_info;
   tdm_error tdm_err = TDM_ERROR_NONE;
   int ret = 0;

   if (eom_output->mirror_run == UP)
     return;

   output = eom_output->output;
   hal_layer = _e_eom_output_get_layer(output, width, height);
   GOTOIFTRUE(hal_layer == NULL, err, "ERROR: get hal layer\n");

   ret = _e_eom_util_create_buffers(eom_output, width, height);
   GOTOIFTRUE(ret == EINA_FALSE, err, "ERROR: create buffers \n");

   tdm_err = tdm_layer_get_info(hal_layer, &layer_info);
   GOTOIFTRUE(tdm_err != TDM_ERROR_NONE, err, "ERROR: get layer info: %d", tdm_err);

   EOM_DBG("LAYER INFO: %dx%d, pos (x:%d, y:%d, w:%d, h:%d,  dpos (x:%d, y:%d, w:%d, h:%d))",
           layer_info.src_config.size.h,  layer_info.src_config.size.v,
           layer_info.src_config.pos.x, layer_info.src_config.pos.y,
           layer_info.src_config.pos.w, layer_info.src_config.pos.h,
           layer_info.dst_pos.x, layer_info.dst_pos.y,
           layer_info.dst_pos.w, layer_info.dst_pos.h);

   eom_output->layer = hal_layer;
   eom_output->output = output;
   eom_output->current_buffer = 0;

   tdm_err = tdm_layer_set_buffer(hal_layer, eom_output->dst_buffers[eom_output->current_buffer]);
   GOTOIFTRUE(tdm_err != TDM_ERROR_NONE, err, "ERROR: set buffer on layer:%d\n", tdm_err);

   tdm_err = tdm_output_set_dpms(output, TDM_OUTPUT_DPMS_ON);
   GOTOIFTRUE(tdm_err != TDM_ERROR_NONE, err, "ERROR: tdm_output_set_dpms on\n");

   /* get main surface */
   ret = _e_eom_output_start_pp(eom_output);
   GOTOIFTRUE(ret == EINA_FALSE, err, "ERROR: get root surfcae\n");

   tdm_err = tdm_output_commit(output, 0, _e_eom_cb_commit, eom_output);
   GOTOIFTRUE(tdm_err != TDM_ERROR_NONE, err, "ERROR: commit crtc:%d\n", tdm_err);

   _e_eom_output_state_set_mode(eom_output, EOM_OUTPUT_MODE_MIRROR);
   _e_eom_output_state_set_attribute(eom_output, EOM_OUTPUT_ATTRIBUTE_NONE);
   eom_output->mirror_run = UP;

   return;

err:
/*
 * TODO: add deinitialization
 */
   return;
}

static void
_e_eom_output_stop_mirror(E_EomOutputPtr eom_output)
{
   if (eom_output->mirror_run == DOWN)
     return;

   eom_output->id = -1;

   _e_eom_output_state_set_status(eom_output, TDM_OUTPUT_CONN_STATUS_DISCONNECTED);
   _e_eom_output_state_set_mode(eom_output, EOM_OUTPUT_MODE_NONE);
   _e_eom_output_state_set_attribute(eom_output, EOM_OUTPUT_ATTRIBUTE_NONE);

   _e_eom_output_deinit(eom_output);

   eom_output->mirror_run = DOWN;
}

static void
_e_eom_output_deinit(E_EomOutputPtr eom_output)
{
   tdm_error err = TDM_ERROR_NONE;
   E_EomClientPtr iterator = NULL;
   Eina_List *l;
   int i = 0;

   if (eom_output->layer)
     {
        err = tdm_layer_unset_buffer(eom_output->layer);
        if (err != TDM_ERROR_NONE)
          EOM_DBG("EXT OUTPUT DEINIT: fail unset buffer:%d\n", err);
        else
          EOM_DBG("EXT OUTPUT DEINIT: ok unset buffer:%d\n", err);

        err = tdm_output_commit(eom_output->output, 0, NULL, eom_output);
        if (err != TDM_ERROR_NONE)
          EOM_DBG ("EXT OUTPUT DEINIT: fail commit:%d\n", err);
        else
          EOM_DBG("EXT OUTPUT DEINIT: ok commit:%d\n", err);
    }

   /* TODO: do I need to do DPMS off? */

   err = tdm_output_set_dpms(eom_output->output, TDM_OUTPUT_DPMS_OFF);
   if (err != TDM_ERROR_NONE)
     EOM_ERR("EXT OUTPUT DEINIT: failed set DPMS off:%d\n", err);

   for (i = 0; i < NUM_MAIN_BUF; i++)
     {
        tdm_buffer_remove_release_handler(eom_output->dst_buffers[i],
                                          _e_eom_cb_pp, eom_output);

        if (eom_output->dst_buffers[i])
          tbm_surface_destroy(eom_output->dst_buffers[i]);
    }

   /*TODO: what is that for?*/
   if (g_eom->eom_state == DOWN)
     EINA_LIST_FOREACH(g_eom->clients, l, iterator)
       {
          if (iterator && iterator->output_id == eom_output->id)
            _e_eom_client_free_buffers(iterator);
       }
}

static tdm_layer *
_e_eom_output_get_layer(tdm_output *output, int width, int height)
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

static E_EomOutputPtr
_e_eom_output_get_by_id(int id)
{
   Eina_List *l;
   E_EomOutputPtr output;

   EINA_LIST_FOREACH(g_eom->outputs, l, output)
     {
        if (output && output->id == id)
          return output;
     }

   return NULL;
}

static E_EomOutputPtr
_e_eom_output_get_by_name(const char *name)
{
   Eina_List *l;
   E_EomOutputPtr output;

   EINA_LIST_FOREACH(g_eom->outputs, l, output)
     {
        if (output && strcmp(output->name, name) == 0)
          return output;
     }

   return NULL;
}

static Eina_Bool
_e_eom_output_start_pp(E_EomOutputPtr eom_output)
{
   /* should be changed in HWC enable environment */
   tbm_surface_info_s src_buffer_info;
   tbm_surface_h src_buffer = NULL;
   Eina_Bool ret = EINA_FALSE;

   src_buffer = _e_eom_util_get_output_surface(g_eom->int_output_name);
   RETURNVALIFTRUE(src_buffer == NULL, EINA_FALSE, "ERROR: get root tdm surfcae\n");

   tbm_surface_get_info(src_buffer, &src_buffer_info);

   EOM_DBG("FRAMEBUFFER TDM: %dx%d   bpp:%d   size:%d",
           src_buffer_info.width, src_buffer_info.height,
           src_buffer_info.bpp, src_buffer_info.size);

   /* TODO: if internal and external outputs are equal */
   ret = _e_eom_pp_is_needed(g_eom->width, g_eom->height,
                             eom_output->width, eom_output->height);
   RETURNVALIFTRUE(ret == EINA_FALSE, EINA_TRUE, "pp is not required\n");

   ret = _e_eom_pp_init(eom_output, src_buffer);
   RETURNVALIFTRUE(ret == EINA_FALSE, EINA_FALSE, "ERROR: init pp\n");

   return EINA_TRUE;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

static Eina_Bool
_e_eom_pp_init(E_EomOutputPtr eom_output, tbm_surface_h src_buffer)
{
   tdm_error err = TDM_ERROR_NONE;
   tdm_info_pp pp_info;
   tdm_pp *pp = NULL;
   int x, y, w, h;

   pp = tdm_display_create_pp(g_eom->dpy, &err);
   RETURNVALIFTRUE(err != TDM_ERROR_NONE, EINA_FALSE, "ERROR: create pp:%d\n", err);

   eom_output->pp = pp;

   /* TO DO : consider rotation */
   _e_eom_util_calculate_fullsize(g_eom->width, g_eom->height,
                             eom_output->width, eom_output->height,
                             &x, &y, &w, &h);
   EOM_DBG("x:%d, y:%d, w:%d, h:%d\n", x, y, w, h);

   pp_info.src_config.size.h = g_eom->width;
   pp_info.src_config.size.v = g_eom->height;
   pp_info.src_config.pos.x = 0;
   pp_info.src_config.pos.y = 0;
   pp_info.src_config.pos.w = g_eom->width;
   pp_info.src_config.pos.h = g_eom->height;
   pp_info.src_config.format = TBM_FORMAT_ARGB8888;

   pp_info.dst_config.size.h = eom_output->width;
   pp_info.dst_config.size.v = eom_output->height;
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

   eom_output->pp_buffer = !eom_output->current_buffer;
   EOM_DBG("PP: curr:%d  pp:%d\n",
           eom_output->current_buffer,
           eom_output->pp_buffer);

   err = tdm_buffer_add_release_handler(eom_output->dst_buffers[eom_output->pp_buffer],
                                      _e_eom_cb_pp, eom_output);
   RETURNVALIFTRUE(err != TDM_ERROR_NONE, EINA_FALSE, "ERROR: set pp hadler:%d\n", err);

   err = tdm_pp_attach(pp, src_buffer,
                       eom_output->dst_buffers[eom_output->pp_buffer]);
   RETURNVALIFTRUE(err != TDM_ERROR_NONE, EINA_FALSE, "ERROR: pp attach:%d\n", err);

   err = tdm_pp_commit(eom_output->pp);
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

//////////////////////////////////////////////////////////////////////////////////////////////////////

static tbm_surface_h
_e_eom_util_create_fake_buffer(int width, int height)
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

   return buffer;
err:
   return NULL;
}

static Eina_Bool
_e_eom_util_create_buffers(E_EomOutputPtr eom_output, int width, int height)
{
   tbm_surface_info_s buffer_info;
   tbm_surface_h buffer = NULL;
   int i = 0;

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

   eom_output->dst_buffers[0] = buffer;

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

   eom_output->dst_buffers[1] = buffer;

   return EINA_TRUE;

err:
   for (i = 0; i < NUM_MAIN_BUF; i++)
     if (eom_output->dst_buffers[i])
       tbm_surface_destroy(eom_output->dst_buffers[i]);

   return EINA_FALSE;
}

static E_EomClientBufferPtr
_e_eom_util_create_client_buffer(E_Comp_Wl_Buffer *wl_buffer, tbm_surface_h tbm_buffer)
{
   E_EomClientBufferPtr buffer = NULL;

   buffer = E_NEW(E_EomClientBuffer, 1);
   if(buffer == NULL)
      return NULL;

   buffer->wl_buffer = wl_buffer;
   buffer->tbm_buffer = tbm_buffer;
   /* TODO: It is not used right now */
   buffer->stamp = _e_eom_util_get_stamp();

   /* I am not sure if it is necessary */
   /* tbm_surface_internal_ref(tbm_buffer); */

   /* TODO: Do we need reference that buffer? */
   /*e_comp_wl_buffer_reference(buffer->tbm_buffer, NULL);*/

   return buffer;
}

static tbm_surface_h
_e_eom_util_get_output_surface(const char *name)
{
   Ecore_Drm_Output *primary_output = NULL;
   Ecore_Drm_Device *dev;
   const Eina_List *l;
   tdm_output *tdm_output_obj = NULL;
   tbm_surface_h tbm = NULL;
   tdm_error err = TDM_ERROR_NONE;
   int count = 0, i = 0;

   EINA_LIST_FOREACH(ecore_drm_devices_get(), l, dev)
     {
        primary_output = ecore_drm_device_output_name_find(dev, name);
        if (primary_output != NULL)
          break;
     }

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
}

static void
_e_eom_util_calculate_fullsize(int src_h, int src_v, int dst_size_h, int dst_size_v,
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

static int
_e_eom_util_get_stamp()
{
   struct timespec tp;

   clock_gettime(CLOCK_MONOTONIC, &tp);

   return ((tp.tv_sec * 1000) + (tp.tv_nsec / 1000));
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

static void
_e_eom_client_add_buffer(E_EomClientPtr client, E_EomClientBufferPtr buffer)
{
   _e_eom_client_free_buffers(client);

   client->buffers = eina_list_append(client->buffers, buffer);
}

static void
_e_eom_client_free_buffers(E_EomClientPtr client)
{
   E_EomClientBufferPtr *buffer = NULL;
   Eina_List *l;

   EINA_LIST_FOREACH(client->buffers, l, buffer)
     {
        if (buffer)
          {
             /* I am not sure if it is necessary */
             /* tbm_surface_internal_unref(buffer->tbm_buffer); */

             /* TODO: Do we need reference that buffer? */
             /*e_comp_wl_buffer_reference(buffer->tbm_buffer, NULL);*/

             client->buffers = eina_list_remove(client->buffers, buffer);
             E_FREE(buffer);
          }
     }
}

static E_EomClientBufferPtr
_e_eom_client_get_buffer(E_EomClientPtr client)
{
   E_EomClientBufferPtr buffer = NULL;
   Eina_List *l;

   //There must be only one buffer
   EINA_LIST_FOREACH(client->buffers, l, buffer)
     {
        if (buffer)
          return buffer;
     }

   return NULL;
}

static E_EomClientPtr
_e_eom_client_get_by_resource(struct wl_resource *resource)
{
   Eina_List *l;
   E_EomClientPtr client;

   EINA_LIST_FOREACH(g_eom->clients, l, client)
     {
        if (client && client->resource == resource)
          return client;
     }

   return NULL;
}

static E_EomClientPtr
_e_eom_client_current_by_id_get(int id)
{
   Eina_List *l;
   E_EomClientPtr client;

   EINA_LIST_FOREACH(g_eom->clients, l, client)
     {
        if (client &&
            client->current == EINA_TRUE &&
            client->output_id == id)
          return client;
     }

   return NULL;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

E_API Eina_Bool e_eom_output_is_external(struct wl_resource *output_resource)
{
   E_Comp_Wl_Output *wl_output = NULL;
   E_EomOutputPtr output;
   Eina_List *l;

   if (output_resource == NULL)
     return EINA_FALSE;

   wl_output = wl_resource_get_user_data(output_resource);
   if (wl_output == NULL)
     return EINA_FALSE;

   if (wl_output->id == NULL)
     return EINA_FALSE;

   EINA_LIST_FOREACH(g_eom->outputs, l, output)
     {
        if(wl_output->id == output->name)
          return EINA_TRUE;
     }

   return EINA_FALSE;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

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
                           _e_eom_cb_wl_bind);

   uint32_t id = wl_display_get_serial(e_comp_wl->wl.disp);
   EOM_DBG("eom name: %d", id);

   EINA_SAFETY_ON_NULL_GOTO(g_eom->global, err);

   ret = _e_eom_init_internal();
   GOTOIFTRUE(ret == EINA_FALSE, err, "failed init_internal()");

   E_LIST_HANDLER_APPEND(g_eom->handlers, ECORE_DRM_EVENT_ACTIVATE, _e_eom_cb_ecore_drm_activate, g_eom);
   E_LIST_HANDLER_APPEND(g_eom->handlers, ECORE_DRM_EVENT_OUTPUT, _e_eom_cb_ecore_drm_output, g_eom);
   E_LIST_HANDLER_APPEND(g_eom->handlers, E_EVENT_CLIENT_BUFFER_CHANGE, _e_eom_cb_client_buffer_change, NULL);

   g_eom->int_output_name = NULL;

   return EINA_TRUE;

err:
   _e_eom_deinit();
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

   if (_e_eom_output_init(g_eom->dpy) != EINA_TRUE)
     {
        EOM_ERR("_e_eom_output_init fail\n");
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

