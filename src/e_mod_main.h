#ifndef E_MOD_MAIN_H
#define E_MOD_MAIN_H

#define CHECK_ERR(val) if (WL_KEYROUTER_ERROR_NONE != val) return;
#define CHECK_ERR_VAL(val) if (WL_KEYROUTER_ERROR_NONE != val) return val;
#define CHECK_NULL(val) if (!val) return;
#define CHECK_NULL_VAL(val) if (!val) return val;

#define EOM_ERR(msg, ARG...) ERR("[eom module][%s:%d] "msg, __FUNCTION__, __LINE__, ##ARG)
#define EOM_WARN(msg, ARG...) WARN("[eom module][%s:%d] "msg, __FUNCTION__, __LINE__, ##ARG)
#define EOM_DBG(msg, ARG...) DBG("[eom module][%s:%d] "msg, __FUNCTION__, __LINE__, ##ARG)

/* E Module */
EAPI extern E_Module_Api e_modapi;
EAPI void *e_modapi_init(E_Module *m);
EAPI int   e_modapi_shutdown(E_Module *m);
EAPI int   e_modapi_save(E_Module *m);

#endif
