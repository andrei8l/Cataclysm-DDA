#ifndef CATA_SRC_TRANSACTION_UI_H
#define CATA_SRC_TRANSACTION_UI_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <queue>
#include <string>

#include "advuilist.h"
#include "advuilist_const.h"
#include "advuilist_sourced.h"
#include "output.h"
#include "point.h"
#include "ui_manager.h"

class JsonObject;
class JsonOut;

struct transaction_ui_save_state {
    public:
        advuilist_save_state left, right;
        uint64_t cpane = 0;
        bool initialized = false;

        void serialize( JsonOut &json ) const;
        void deserialize( JsonObject const &jo );
};

/// two-pane transaction ui based on advuilist_sourced
template <class Container, typename T = typename Container::value_type>
class transaction_ui
{
    public:
        using advuilist_t = advuilist_sourced<Container, T>;
        using select_t = typename advuilist_t::select_t;
        using fselect_t = std::function<void( transaction_ui<Container, T> *, select_t const & )>;
        using fctxt_t = std::function<void( transaction_ui<Container, T> *, std::string const & )>;
        using fresize_t = std::function<void( transaction_ui<Container, T> * )>;

        enum class event { QUIT = 0, SWAP = 1, SWITCH = 2, ACTIVITY = 3, NEVENTS = 4 };

        // NOLINTNEXTLINE(cata-use-named-point-constants)
        explicit transaction_ui( point const &srclayout, point const &size = { -1, -1 },
                                 point const &origin = { -1, -1 },
                                 std::string const &ctxtname = advuilist_literals::CTXT_DEFAULT,
                                 point const &reserved_rows = { 2, 1 } );

        advuilist_t *left();
        advuilist_t *right();
        advuilist_t *cur_pane();
        advuilist_t *other_pane();

        void on_input( fctxt_t const &func );
        void on_select( fselect_t const &func );
        void on_resize( fresize_t const &func );
        void push_event( event const &ev );

        void show();
        void hide();
        void force_rebuild( bool state );
        void resize( point const &size, point const &origin );
        void save_state( transaction_ui_save_state *state ) const;
        void load_state( transaction_ui_save_state const &state, bool reb = true );

    private:
        constexpr static std::size_t npanes = 2;

        using panecont_t = std::array<std::unique_ptr<advuilist_t>, 2>;

        constexpr static typename panecont_t::size_type const _left = 0;
        constexpr static typename panecont_t::size_type const _right = 1;

        point _size;
        point _origin;
        panecont_t _panes;
        fselect_t _fselect;
        fresize_t _fresize;
        std::queue<event> _queue;
        fctxt_t _fctxt;
        typename panecont_t::size_type _cpane = 0;
        bool _exit = true;

        void _ctxthandler( advuilist<Container, T> *ui, std::string const &action );
        void _process( event const &ev );
        void _swap_panes();
};

// *INDENT-OFF*
template <class Container, typename T>
transaction_ui<Container, T>::transaction_ui( point const &srclayout, point const &size,
                                              point const &origin, std::string const &ctxtname,
                                              point const &reserved_rows )
    : _size( size ),
      _origin( origin ),
      _panes{ std::make_unique<advuilist_t>( srclayout, _size, _origin, ctxtname, reserved_rows ),
              std::make_unique<advuilist_t>( srclayout, _size, _origin, ctxtname, reserved_rows ) }
{
// *INDENT-ON*
    resize( _size, _origin );
    using namespace advuilist_literals;
    for( std::unique_ptr<advuilist_t> &v : _panes )
    {
        v->get_ctxt()->register_action( ACTION_SWITCH_PANES );
        v->get_ctxt()->register_action( PANE_LEFT );
        v->get_ctxt()->register_action( PANE_RIGHT );
        v->on_input( [this]( advuilist<Container, T> *ui, std::string const & action ) {
            this->_ctxthandler( ui, action );
        } );
        v->on_resize( []( advuilist<Container, T> * /**/ ) {} );
    }
}

template <class Container, typename T>
typename transaction_ui<Container, T>::advuilist_t *transaction_ui<Container, T>::left()
{
    return _panes[_left].get();
}

template <class Container, typename T>
typename transaction_ui<Container, T>::advuilist_t *transaction_ui<Container, T>::right()
{
    return _panes[_right].get();
}

template <class Container, typename T>
typename transaction_ui<Container, T>::advuilist_t *transaction_ui<Container, T>::cur_pane()
{
    return _panes[_cpane].get();
}

template <class Container, typename T>
typename transaction_ui<Container, T>::advuilist_t *transaction_ui<Container, T>::other_pane()
{
    return _panes[1 - _cpane].get();
}

template <class Container, typename T>
void transaction_ui<Container, T>::on_input( fctxt_t const &func )
{
    _fctxt = func;
}

template <class Container, typename T>
void transaction_ui<Container, T>::on_select( fselect_t const &func )
{
    _fselect = func;
}

template <class Container, typename T>
void transaction_ui<Container, T>::on_resize( fresize_t const &func )
{
    _fresize = func;
}

template <class Container, typename T>
void transaction_ui<Container, T>::push_event( event const &ev )
{
    _queue.emplace( ev );
}

template <class Container, typename T>
void transaction_ui<Container, T>::show()
{
    _panes[1 - _cpane]->init_ui();
    _panes[_cpane]->init_ui();

    _exit = false;

    ui_adaptor dummy;
    dummy.on_screen_resize( [&]( ui_adaptor & /*ui*/ ) {
        if( _fresize ) {
            _fresize( this );
        } else {
            resize( _size, _origin );
        }
    } );
    dummy.mark_resize();

    force_rebuild( false );
    while( !_exit ) {
        typename advuilist_t::select_t selection = _panes[_cpane]->select();
        if( _fselect and !selection.empty() ) {
            _fselect( this, selection );
        }

        while( !_queue.empty() ) {
            event const ev = _queue.front();
            _queue.pop();
            _process( ev );
        }
    }
}

template <class Container, typename T>
void transaction_ui<Container, T>::_swap_panes()
{
    std::swap( _panes[_left], _panes[_right] );
    resize( _size, _origin );
}

template <class Container, typename T>
void transaction_ui<Container, T>::resize( point const &size, point const &origin )
{
    _size = { size.x > 0 ? size.x > TERMX ? TERMX : size.x : ( TERMX * 3 ) / 4,
              size.y > 0 ? size.y > TERMY ? TERMY : size.y : TERMY
            };
    _origin = { origin.x >= 0 ? origin.x + _size.x > TERMX ? 0 : origin.x : TERMX / 2 - _size.x / 2,
                origin.y >= 0 ? origin.y + _size.y > TERMY ? 0 : origin.y
                : TERMY / 2 - _size.y / 2
              };
    _panes[_left]->resize( { _size.x / 2, _size.y }, _origin );
    _panes[_right]->resize( { _size.x / 2, _size.y }, { _origin.x + _size.x / 2, _origin.y } );
}

template <class Container, typename T>
void transaction_ui<Container, T>::save_state( transaction_ui_save_state *state ) const
{
    _panes[_left]->save_state( &state->left );
    _panes[_right]->save_state( &state->right );
    state->cpane = static_cast<uint64_t>( _cpane );
    state->initialized = true;
}

template <class Container, typename T>
void transaction_ui<Container, T>::load_state( transaction_ui_save_state const &state, bool reb )
{
    _panes[_left]->load_state( state.left, reb );
    _panes[_right]->load_state( state.right, reb );
    _cpane = static_cast<typename panecont_t::size_type>( state.cpane );
}

template <class Container, typename T>
void transaction_ui<Container, T>::_ctxthandler( advuilist<Container, T> *ui,
        std::string const &action )
{
    using namespace advuilist_literals;
    if( action == ACTION_QUIT ) {
        _queue.emplace( event::QUIT );
    } else if( action == ACTION_SWITCH_PANES or action == PANE_LEFT or action == PANE_RIGHT ) {
        typename panecont_t::size_type check = action == PANE_LEFT ? _right : _left;
        if( action == ACTION_SWITCH_PANES or _cpane == check ) {
            _queue.emplace( event::SWITCH );
            ui->suspend();
        }
    } else if( action == ACTION_MOUSE_SELECT or action == ACTION_MOUSE_MOVE ) {
        cata::optional<point> const o_p = ui->get_ctxt()->get_coordinates_text( catacurses::stdscr );
        std::pair<point, point> const o_s = other_pane()->get_size();
        inclusive_rectangle<point> o_r{ o_s.second, o_s.first + o_s.second };
        if( o_p and o_r.contains( *o_p ) ) {
            _queue.emplace( event::SWITCH );
            ui->suspend();
        }
    }

    if( _fctxt ) {
        _fctxt( this, action );
    }
}

template <class Container, typename T>
void transaction_ui<Container, T>::_process( event const &ev )
{
    switch( ev ) {
        case event::QUIT: {
            hide();
            _exit = true;
            break;
        }
        case event::SWAP: {
            _swap_panes();
            break;
        }
        case event::SWITCH: {
            _cpane = -_cpane + 1;
            break;
        }
        case event::ACTIVITY: {
            force_rebuild( true );
            _exit = true;
            break;
        }
        case event::NEVENTS: {
            break;
        }
    }
}

template <class Container, typename T>
void transaction_ui<Container, T>::hide()
{
    _panes[_left]->hide();
    _panes[_right]->hide();
}

template <class Container, typename T>
void transaction_ui<Container, T>::force_rebuild( bool state )
{
    _panes[_left]->force_rebuild( state );
    _panes[_right]->force_rebuild( state );
}

#endif // CATA_SRC_TRANSACTION_UI_H
