#ifndef E_MOD_MAIN_H
#define E_MOD_MAIN_H

#include "eom-server-protocol.h"

#define CHECK_ERR(val) if (WL_KEYROUTER_ERROR_NONE != val) return;
#define CHECK_ERR_VAL(val) if (WL_KEYROUTER_ERROR_NONE != val) return val;
#define CHECK_NULL(val) if (!val) return;
#define CHECK_NULL_VAL(val) if (!val) return val;

#define EOM_ERR(msg, ARG...) ERR("[eom module][%s:%d] "msg"\n", __FUNCTION__, __LINE__, ##ARG)
#define EOM_WARN(msg, ARG...) WARN("[eom module][%s:%d] "msg"\n", __FUNCTION__, __LINE__, ##ARG)
#define EOM_DBG(msg, ARG...) DBG("[eom module][%s:%d] "msg"\n", __FUNCTION__, __LINE__, ##ARG)

#define ALEN(array) (sizeof(array) / sizeof(array)[0])

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
   unsigned int w;
   unsigned int h;
   unsigned int phys_width;
   unsigned int phys_height;

   const char *name;

   tdm_output *output;
   tdm_layer *layer;
   tdm_pp *pp;

   E_EomFlag mirror_run;
   tdm_output_conn_status status;
   eom_output_attribute_e attribute;
   eom_output_attribute_state_e attribute_state;

   /* external output data */
   E_Comp_Wl_Output *wl_output;
   E_EomOutMode dst_mode;

   /* mirror mode data */
   tbm_surface_h dst_buffers[NUM_MAIN_BUF];
   int current_buffer;
   int pp_buffer;

   tbm_surface_h fake_buffer;
};

struct _E_Eom
{
   struct wl_global *global;

   E_EomFlag eom_state;

   tdm_display *dpy;
   tbm_bufmgr bufmgr;
   int fd;

   /* Internal output data */
   char *int_output_name;
   E_EomOutMode src_mode;

   Eina_List *clients;
   Eina_List *handlers;
   Eina_List *outputs;
   unsigned int output_count;
};

struct _E_Eom_Client
{
   struct wl_resource *resource;
   Eina_Bool current;

   /* Output a client related to */
   int output_id;

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

/* handle external output */
static tdm_layer * _e_eom_hal_layer_get(tdm_output *output, int width, int height);
static Eina_Bool _e_eom_create_output_buffers(E_EomOutputPtr eom_output, int width, int height);

/* handle internal output, pp */
static Eina_Bool _e_eom_mirror_start(E_EomOutputPtr eom_output);
static tbm_surface_h _e_eom_root_internal_tdm_surface_get(const char *name);
static Eina_Bool _e_eom_pp_src_to_dst(E_EomOutputPtr eom_output, tbm_surface_h src_buffer);
static Eina_Bool _e_eom_pp_is_needed(int src_w, int src_h, int dst_w, int dst_h);
static void _e_eom_calculate_fullsize(int src_h, int src_v, int dst_size_h, int dst_size_v,
                                      int *dst_x, int *dst_y, int *dst_w, int *dst_h);

/* tdm handlers */
static void _e_eom_pp_cb(tbm_surface_h surface, void *user_data);
static void _e_eom_commit_cb(tdm_output *output EINA_UNUSED, unsigned int sequence EINA_UNUSED,
                                    unsigned int tv_sec EINA_UNUSED, unsigned int tv_usec EINA_UNUSED,
                                    void *user_data);

/* handle clients buffers
 * The work flow is very simple, when a client does commit we take a client's
 * buffer and add it to list. During page flip we show that buffer. When a
 * new client's buffer has been send we destroy previous buffer and add new
 * one to the list. And so on
 * We created that list for possible extending in future
 */
static E_EomClientBufferPtr _e_eom_client_create_buffer(E_Comp_Wl_Buffer *wl_buffer, tbm_surface_h tbm_buffer);
static void _e_eom_client_buffer_add(E_EomClientPtr client, E_EomClientBufferPtr buffer);
static void _e_eom_client_buffers_free(E_EomClientPtr client);
static E_EomClientBufferPtr _e_eom_client_buffer_get(E_EomClientPtr client);

/*eom utils functions*/
E_EomClientPtr _e_eom_client_current_by_id_get(int id);
E_EomClientPtr _e_eom_client_by_resource_get(struct wl_resource *resource);
E_EomOutputPtr _e_eom_output_by_id_get(int id);
static tbm_surface_h _e_eom_create_fake_buffer(int width, int height);
static int _e_eom_get_time_in_mseconds();

/*TODO: is there a way to use it in Enlightenment?*/
/*exported API*/
E_API Eina_Bool e_eom_is_external_output(struct wl_resource *output_resource);

#endif
