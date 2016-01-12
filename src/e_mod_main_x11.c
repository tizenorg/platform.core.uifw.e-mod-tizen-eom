#include "e.h"
#include "e_mod_main.h"

E_API E_Module_Api e_modapi = { E_MODULE_API_VERSION, "EOM Module of Window Manager" };

E_API void *
e_modapi_init(E_Module *m)
{

   return m;
}

E_API int
e_modapi_shutdown(E_Module *m EINA_UNUSED)
{

   return 1;
}

E_API int
e_modapi_save(E_Module *m EINA_UNUSED)
{
   /* Save something to be kept */
   return 1;
}

