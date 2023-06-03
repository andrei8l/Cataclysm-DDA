#ifndef CATA_SRC_MATH_PARSER_DIAG_H
#define CATA_SRC_MATH_PARSER_DIAG_H

#include <array>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "dialogue_helpers.h"

class math_exp;
struct dialogue;
struct dialogue_func {
    dialogue_func( std::string_view s_, std::string_view sc_, int n_ ) : symbol( s_ ),
        scopes( sc_ ), num_params( n_ ) {}
    std::string_view symbol;
    std::string_view scopes;
    int num_params{};
};

struct diag_value {
    double dbl() const;
    double dbl( dialogue const &d ) const;
    std::string_view str() const;
    std::string str( dialogue const &d ) const;
    var_info var() const;

    using impl_t = std::variant<double, std::string, var_info, math_exp>;
    impl_t data;
};

constexpr bool operator==( diag_value const &lhs, std::string_view rhs )
{
    return std::holds_alternative<std::string>( lhs.data ) && std::get<std::string>( lhs.data ) == rhs;
}

// helper struct that makes it easy to determine whether a kwarg's value has been used
struct deref_diag_value {
    public:
        deref_diag_value() = default;
        explicit deref_diag_value( diag_value &&dv ) : _val( dv ) {}
        diag_value const *operator->() const {
            _used = true;
            return &_val;
        }
        diag_value const &operator*() const {
            _used = true;
            return _val;
        }
        bool was_used() const {
            return _used;
        }
    private:
        bool mutable _used = false;
        diag_value _val;
};

using diag_kwargs = std::map<std::string, deref_diag_value>;

struct dialogue_func_eval : dialogue_func {
    using f_t = std::function<double( dialogue & )> ( * )( char scope,
                std::vector<diag_value> const &, diag_kwargs const & );

    dialogue_func_eval( std::string_view s_, std::string_view sc_, int n_, f_t f_ )
        : dialogue_func( s_, sc_, n_ ), f( f_ ) {}

    f_t f;
};

struct dialogue_func_ass : dialogue_func {
    using f_t = std::function<void( dialogue &, double )> ( * )( char scope,
                std::vector<diag_value> const &, diag_kwargs const & );

    dialogue_func_ass( std::string_view s_, std::string_view sc_, int n_, f_t f_ )
        : dialogue_func( s_, sc_, n_ ), f( f_ ) {}

    f_t f;
};

using pdiag_func_eval = dialogue_func_eval const *;
using pdiag_func_ass = dialogue_func_ass const *;

using decl_diag_eval = std::function<double( dialogue & )> ( char scope,
                       std::vector<diag_value> const &params, diag_kwargs const &kwargs );
using decl_diag_ass = std::function<void( dialogue &, double )> ( char scope,
                      std::vector<diag_value> const &params, diag_kwargs const &kwargs );

decl_diag_eval armor_eval;
decl_diag_eval attack_speed_eval;
decl_diag_eval num_input_eval;
decl_diag_eval option_eval;
decl_diag_eval pain_eval;
decl_diag_ass pain_ass;
decl_diag_eval skill_eval;
decl_diag_ass skill_ass;
decl_diag_eval test_diag;
decl_diag_eval u_val;
decl_diag_ass u_val_ass;
decl_diag_eval weather_eval;
decl_diag_ass weather_ass;

inline std::array<dialogue_func_eval, 9> const dialogue_eval_f{
    dialogue_func_eval{ "_test_diag_", "g", -1, test_diag },
    dialogue_func_eval{ "val", "un", -1, u_val },
    dialogue_func_eval{ "game_option", "g", 1, option_eval },
    dialogue_func_eval{ "pain", "un", 0, pain_eval },
    dialogue_func_eval{ "skill", "un", 1, skill_eval },
    dialogue_func_eval{ "weather", "g", 1, weather_eval },
    dialogue_func_eval{ "armor", "un", 2, armor_eval },
    dialogue_func_eval{ "num_input", "g", 2, num_input_eval },
    dialogue_func_eval{ "attack_speed", "un", 0, attack_speed_eval }
};

inline std::array<dialogue_func_ass, 4> const dialogue_assign_f{
    dialogue_func_ass{ "val", "un", -1, u_val_ass },
    dialogue_func_ass{ "pain", "un", 0, pain_ass },
    dialogue_func_ass{ "skill", "un", 1, skill_ass },
    dialogue_func_ass{ "weather", "g", 1, weather_ass },
};

#endif // CATA_SRC_MATH_PARSER_DIAG_H
