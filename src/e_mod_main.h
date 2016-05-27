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
typedef struct _E_Eom_Event_Data E_EomEventData, *E_EomEventDataPtr;
typedef struct _E_Eom_Output E_EomOutput, *E_EomOutputPtr;
typedef struct _E_Eom_Fake_Buffers E_EomFakeBuffers, *E_EomFakeBuffersPtr;
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

   tdm_output *output;

   tdm_output_conn_status status;
   E_EomFlag mirror_run;
   eom_output_attribute_e attribute;
   eom_output_attribute_state_e attribute_state;

   /* external output data */
   char *ext_output_name;
   E_EomFlag is_external_init;
   E_EomOutMode src_mode;
   E_Comp_Wl_Output *wl_output;

   /* internal output data */
   char *int_output_name;
   E_EomFlag is_internal_grab;
   E_EomOutMode dst_mode;
};

struct _E_Eom
{
   struct wl_global *global;
   Eina_List *eom_clients;
   Eina_List *handlers;

   tdm_display *dpy;
   tbm_bufmgr bufmgr;
   int fd;

   Eina_List *outputs;
   unsigned int output_count;

#if 1
   /* eom state */
   E_EomFlag eom_sate;
   enum wl_eom_mode eom_mode;
   enum wl_eom_attribute eom_attribute;
   enum wl_eom_attribute_state eom_attribute_state;
   enum wl_eom_status eom_status;

   /*data related to cooperating with clients */
   E_EomFlag is_mirror_mode;
   struct wl_resource *current_client;

   /* external output data */
   char *ext_output_name;
   E_EomFlag is_external_init;
   int id;
   E_EomOutMode src_mode;
   E_Comp_Wl_Output *wl_output;

   /* internal output data */
   char *int_output_name;
   E_EomFlag is_internal_grab;
   E_EomOutMode dst_mode;
#endif
};

struct _E_Eom_Event_Data
{
   tdm_output *output;
   tdm_layer *layer;
   tdm_pp *pp;

   /* mirror mode data*/
   tbm_surface_h dst_buffers[NUM_MAIN_BUF];
   int current_buffer;
   int pp_buffer;

   /* extended mode data */
   Eina_List *client_buffers_list;
};

struct _E_Eom_Client_Buffer
{
   E_Comp_Wl_Buffer *wl_buffer;
   tbm_surface_h tbm_buffer;

   unsigned long stamp;
};

struct _E_Eom_Client
{
	struct wl_resource *resource;
	Eina_Bool curent;
};

struct _E_Eom_Fake_Buffers
{
   tbm_surface_h fake_buffers[NUM_MAIN_BUF];
   int current_fake_buffer;
};

/* handle external output */
static E_Comp_Wl_Output *_e_eom_e_comp_wl_output_get(const Eina_List *outputs, const char *id);
static Eina_Bool _e_eom_set_up_external_output(const char *output_name, int width, int height);
static tdm_output * _e_eom_hal_output_get(const char *id);
static tdm_layer * _e_eom_hal_layer_get(tdm_output *output, int width, int height);
static Eina_Bool _e_eom_create_output_buffers(E_EomEventDataPtr eom_data, int width, int height);
static enum wl_eom_type _e_eom_output_name_to_eom_type(const char *output_name);

/* handle internal output, pp */
static Eina_Bool _e_eom_mirror_start(const char *output_name, int width, int height);
static tbm_surface_h _e_eom_root_internal_tdm_surface_get(const char *name);
static Eina_Bool _e_eom_pp_src_to_dst(tbm_surface_h src_buffer);
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
 * We created that list for future possible extending
 */
static E_EomClientBufferPtr _e_eom_create_client_buffer(E_Comp_Wl_Buffer *wl_buffer, tbm_surface_h tbm_buffer);
static void _e_eom_add_client_buffer_to_list(E_EomClientBufferPtr client_buffer);
static void _e_eom_client_buffers_list_free();
static E_EomClientBufferPtr _e_eom_get_client_buffer_from_list();

/*eom utils functions*/
static int _e_eom_get_time_in_mseconds();
static void _e_eom_create_fake_buffers(int width, int height);


#endif
