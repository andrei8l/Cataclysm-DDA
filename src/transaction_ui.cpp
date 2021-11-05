#include "transaction_ui.h"
#include "json.h"

void transaction_ui_save_state::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "cpane", cpane );
    json.member( "left", left );
    json.member( "right", right );
    json.end_object();
}

void transaction_ui_save_state::deserialize( JsonObject const &jo )
{
    bool init = jo.read( "cpane", cpane );
    init &= jo.read( "left", left );
    init &= jo.read( "right", right );
    initialized = init;
}
