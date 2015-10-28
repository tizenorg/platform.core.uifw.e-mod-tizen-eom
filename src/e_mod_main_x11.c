#include "e.h"
#include "e_mod_main.h"

EAPI E_Module_Api e_modapi = { E_MODULE_API_VERSION, "EOM Module of Window Manager" };

EAPI void *
e_modapi_init(E_Module *m)
{

   return m;
}

EAPI int
e_modapi_shutdown(E_Module *m EINA_UNUSED)
{

   return 1;
}

EAPI int
e_modapi_save(E_Module *m EINA_UNUSED)
{
   /* Save something to be kept */
   return 1;
}

