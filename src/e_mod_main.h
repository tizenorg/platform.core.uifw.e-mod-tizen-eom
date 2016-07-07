#ifndef E_MOD_MAIN_H
#define E_MOD_MAIN_H

#include "eom-server-protocol.h"

extern Eina_Bool eom_server_debug_on;

#define ALEN(array) (sizeof(array) / sizeof(array)[0])

#define CHECK_ERR(val) if (WL_KEYROUTER_ERROR_NONE != val) return;
#define CHECK_ERR_VAL(val) if (WL_KEYROUTER_ERROR_NONE != val) return val;
#define CHECK_NULL(val) if (!val) return;
#define CHECK_NULL_VAL(val) if (!val) return val;

#define EOM_INF(msg, ARG...) INF("[eom module][%s:%d] " msg "\n", __FUNCTION__, __LINE__, ##ARG)
#define EOM_ERR(msg, ARG...) ERR("[eom module][%s:%d] ERR: " msg "\n", __FUNCTION__, __LINE__, ##ARG)

#define EOM_DBG(msg, ARG...)    \
{    \
   if (eom_server_debug_on)    \
     DBG("[eom module][%s:%d] DBG: " msg "\n", __FUNCTION__, __LINE__, ##ARG);    \
}

#define EOM_WARN(msg, ARG...)    \
{    \
   if (eom_server_debug_on)    \
     WARN("[eom module][%s:%d] WARN: " msg "\n", __FUNCTION__, __LINE__, ##ARG);    \
}

#define RETURNIFTRUE(statement, msg, ARG...)    \
if (statement)    \
{    \
   EOM_ERR( msg, ##ARG);    \
   return;    \
}

#define RETURNVALIFTRUE(statement, ret, msg, ARG...)    \
if (statement)    \
{    \
   EOM_ERR( msg, ##ARG);    \
   return ret;    \
}

#define GOTOIFTRUE(statement, lable, msg, ARG...)    \
if (statement)    \
{    \
   EOM_ERR( msg, ##ARG);    \
   goto lable;    \
}

/* E Module */
E_API extern E_Module_Api e_modapi;
E_API void *e_modapi_init(E_Module *m);
E_API int   e_modapi_shutdown(E_Module *m);
E_API int   e_modapi_save(E_Module *m);

#define NUM_MAIN_BUF 2
#define NUM_ATTR 3

typedef struct _E_Eom E_Eom, *E_EomPtr;
typedef struct _E_Eom_Out_Mode E_EomOutMode, *E_EomOutModePtr;
typedef struct _E_Eom_Output E_EomOutput, *E_EomOutputPtr;
typedef struct _E_Eom_Client_Buffer E_EomClientBuffer, *E_EomClientBufferPtr;
typedef struct _E_Eom_Client E_EomClient, *E_EomClientPtr;

typedef enum
{
   DOWN = 0,
   UP,
} E_EomFlag;

typedef enum
{
   NONE = 0,
   MIRROR,
   PRESENTATION,
} E_EomOutputState;

struct _E_Eom_Out_Mode
{
   int w;
   int h;
};

struct _E_Eom_Output
{
   unsigned int id;
   eom_output_type_e type;
   eom_output_mode_e mode;
   unsigned int width;
   unsigned int height;
   unsigned int phys_width;
   unsigned int phys_height;

   const char *name;

   tdm_output *output;
   tdm_layer *layer;
   tdm_pp *pp;

   E_EomOutputState state;
   tdm_output_conn_status status;
   eom_output_attribute_e attribute;
   eom_output_attribute_state_e attribute_state;

   /* external output data */
   E_Comp_Wl_Output *wl_output;

   /* mirror mode data */
   tbm_surface_h dst_buffers[NUM_MAIN_BUF];
   int current_buffer;
   int pp_buffer;

   tbm_surface_h dummy_buffer;
};

struct _E_Eom
{
   struct wl_global *global;

   tdm_display *dpy;
   tbm_bufmgr bufmgr;
   int fd;

   /* Internal output data */
   E_EomFlag main_output_state;
   char *main_output_name;
   int width;
   int height;

   Eina_List *clients;
   Eina_List *handlers;
   Eina_List *outputs;
   unsigned int output_count;
};

struct _E_Eom_Client
{
   struct wl_resource *resource;
   Eina_Bool current;

   /* EOM output the client related to */
   int output_id;
   /* E_Client the client related to */
   E_Client *ec;

   /*TODO: As I understand there are cannot be more than one client buffer on
    *server side, but for future extendabilty store it in the list */
   /*Client's buffers */
   Eina_List *buffers;
};

struct _E_Eom_Client_Buffer
{
   E_Comp_Wl_Buffer *wl_buffer;
   tbm_surface_h tbm_buffer;

   unsigned long stamp;
};

static Eina_Bool _e_eom_init();
static Eina_Bool _e_eom_init_internal();
static void _e_eom_deinit();

static void _e_eom_cb_wl_request_set_attribute(struct wl_client *client,
                                               struct wl_resource *resource,
                                               uint32_t output_id,
                                               uint32_t attribute);
static void _e_eom_cb_wl_request_set_xdg_window(struct wl_client *client,
                                                struct wl_resource *resource,
                                                uint32_t output_id,
                                                struct wl_resource *surface);
static void _e_eom_cb_wl_request_set_shell_window(struct wl_client *client,
                                                  struct wl_resource *resource,
                                                  uint32_t output_id,
                                                  struct wl_resource *surface);
static void _e_eom_cb_wl_request_get_output_info(struct wl_client *client,
                                                 struct wl_resource *resource,
                                                 uint32_t output_id);
static void _e_eom_cb_wl_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id);
static void _e_eom_cb_wl_resource_destory(struct wl_resource *resource);
static Eina_Bool _e_eom_cb_ecore_drm_output(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);
static Eina_Bool _e_eom_cb_ecore_drm_activate(void *data, int type EINA_UNUSED, void *event);
static Eina_Bool _e_eom_cb_client_buffer_change(void *data, int type, void *event);
static void _e_eom_cb_pp(tbm_surface_h surface, void *user_data);
static void _e_eom_cb_commit(tdm_output *output EINA_UNUSED, unsigned int sequence EINA_UNUSED,
                             unsigned int tv_sec EINA_UNUSED, unsigned int tv_usec EINA_UNUSED,
                             void *user_data);
static void _e_eom_cb_tdm_output_status_change(tdm_output *output, tdm_output_change_type type,
                                               tdm_value value, void *user_data);

static Eina_Bool _e_eom_output_init(tdm_display *dpy);
static const tdm_output_mode *_e_eom_output_get_best_mode(tdm_output *output);
static int _e_eom_output_get_position(void);
static void _e_eom_output_start_mirror(E_EomOutputPtr eom_output);
static void _e_eom_output_stop_mirror(E_EomOutputPtr eom_output);
static void _e_eom_output_deinit(E_EomOutputPtr eom_output);
static tdm_layer *_e_eom_output_get_layer(E_EomOutputPtr eom_output);
static E_EomOutputPtr _e_eom_output_get_by_id(int id);
static Eina_Bool _e_eom_output_start_pp(E_EomOutputPtr eom_output);
static Eina_Bool _e_eom_output_create_buffers(E_EomOutputPtr eom_output, int width, int height);

static void _e_eom_window_set_internal(struct wl_resource *resource, int output_id, E_Client *ec);

static Eina_Bool _e_eom_pp_init(E_EomOutputPtr eom_output, tbm_surface_h src_buffer);
static Eina_Bool _e_eom_pp_is_needed(int src_w, int src_h, int dst_w, int dst_h);

static void _e_eom_util_get_debug_env();
static tbm_surface_h _e_eom_util_create_buffer(int width, int height, int format, int flags);
static E_EomClientBufferPtr _e_eom_util_create_client_buffer(E_Comp_Wl_Buffer *wl_buffer,
                                                        tbm_surface_h tbm_buffer);
static void _e_eom_util_calculate_fullsize(int src_h, int src_v, int dst_size_h, int dst_size_v,
                                           int *dst_x, int *dst_y, int *dst_w, int *dst_h);
static tbm_surface_h _e_eom_util_get_output_surface(const char *name);
static int _e_eom_util_get_stamp();
#if 0
static void _e_eom_util_draw(tbm_surface_h surface);
#endif

static void _e_eom_client_add_buffer(E_EomClientPtr client, E_EomClientBufferPtr buffer);
static void _e_eom_client_free_buffers(E_EomClientPtr client);
static E_EomClientBufferPtr _e_eom_client_get_buffer(E_EomClientPtr client);
static E_EomClientPtr _e_eom_client_get_by_resource(struct wl_resource *resource);
static E_EomClientPtr _e_eom_client_get_current_by_id(int id);
static E_EomClientPtr _e_eom_client_get_current_by_ec(E_Client *ec);

#endif

