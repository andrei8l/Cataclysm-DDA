#include "advuilist.h"
#include "json.h"

void advuilist_save_state::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "slot", slot );
    json.member( "idx", idx );
    json.member( "sort", sort );
    json.member( "group", group );
    json.member( "icon", icon );
    json.member( "filter", filter );
    json.end_object();
}

void advuilist_save_state::deserialize( JsonObject const &jo )
{
    initialized = jo.read( "slot", slot );
    jo.read( "idx", idx );
    jo.read( "sort", sort );
    jo.read( "group", group );
    jo.read( "icon", icon );
    jo.read( "filter", filter );
}
