#include <memory>
#include <string>

#include "cached_options.h"
#include "dialogue.h"
#include "game.h"
#include "math_parser.h"
#include "messages.h"
#include "talker.h"

extern "C" int LLVMFuzzerTestOneInput( uint8_t const *Data, size_t Size );

namespace
{
dialogue &get_dialogue()
{
    static dialogue d( std::make_unique<talker>(), std::make_unique<talker>() );
    if( !g ) {
        g = std::make_unique<game>();
    }
    return d;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput( uint8_t const *Data, size_t Size )
{
    std::string input( reinterpret_cast<char const *>( Data ), Size );
    test_mode = true;
    setupDebug( DebugOutput::std_err );
    math_exp fuzzexp;
    fuzzexp.parse( input );
    double volatile ret [[maybe_unused]] = fuzzexp.eval( get_dialogue() );
    return 0;
}
