#ifndef CATA_SRC_ADVUILIST_SOURCED_H
#define CATA_SRC_ADVUILIST_SOURCED_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>

#include "advuilist.h"
#include "advuilist_const.h"
#include "color.h"
#include "cursesdef.h"
#include "output.h"
#include "point.h"
#include "string_formatter.h"
#include "ui_manager.h"

/// wrapper for advuilist that allows for switching between multiple sources
template <class Container, typename T = typename Container::value_type>
class advuilist_sourced : public advuilist<Container, T>
{
    public:
        // source function
        using fsource_t = std::function<Container()>;
        // source availability function
        using fsourceb_t = std::function<bool()>;
        using fdraw_t = std::function<void( advuilist_sourced<Container, T> * )>;
        using icon_t = char;
        using slotidx_t = std::size_t;
        struct getsource_t {
            slotidx_t slotidx = 0;
            icon_t icon = 0;
            bool avail = false;

            bool operator==( getsource_t const &r ) const {
                return slotidx == r.slotidx and icon == r.icon;
            }
        };
        using flabel_t = std::function<std::string()>;
        struct source_t {
            flabel_t label_printer;
            icon_t icon = 0;
            fsource_t source_func;
            fsourceb_t source_avail_func;
        };
        using fctxt_t = typename advuilist<Container, T>::fctxt_t;
        using select_t = typename advuilist<Container, T>::select_t;

        // NOLINTNEXTLINE(cata-use-named-point-constants)
        explicit advuilist_sourced( point const &srclayout, point const &size = { -1, -1 },
                                    point const &origin = { -1, -1 },
                                    std::string const &ctxtname = advuilist_literals::CTXT_DEFAULT,
                                    point const &reserved_rows = { 2, 1 } );

        /// binds a new source to slot
        void add_source( slotidx_t slot, source_t const &src );
        /// sets active source slot
        ///@param slot
        ///@param icon
        ///@param fallthrough used internally by rebuild() to ensure that the internal list is valid
        ///@param reb if true, also runs advuilist::rebuild(). used internally by loadstate()
        bool set_source( slotidx_t slotidx, icon_t icon = 0, bool fallthrough = false, bool reb = true );
        getsource_t get_source() const;
        getsource_t get_source_prev() const;

        select_t select();
        void rebuild();
        std::shared_ptr<ui_adaptor> init_ui();
        void hide();
        // NOLINTNEXTLINE(cata-use-named-point-constants)
        void resize( point const &size, point const &origin, point const &reserved_rows = {-1, -1} );

        std::pair<point, point> get_size() const;

        void on_redraw( fdraw_t const &func );
        void on_resize( fdraw_t const &func );

        void on_input( fctxt_t const &func );

        void save_state( advuilist_save_state *state ) const;
        void load_state( advuilist_save_state const &state, bool reb = true );

    private:
        using slotcont_t = std::map<icon_t, source_t>;
        struct slot_t {
            icon_t cur_icon = 0;
            slotcont_t slotcont;
        };
        /// key is slot index
        using srccont_t = std::map<slotidx_t, slot_t>;

        Container _container;
        srccont_t _sources;
        fctxt_t _fctxt;
        fdraw_t _fdraw, _fresize;
        point _size, _osize;
        point _origin, _oorigin;
        point _map_size;
        point mutable _cursor;
        slotidx_t _cslot = 0;
        slotidx_t _hslot = std::numeric_limits<slotidx_t>::max(); // highlighted slot
        getsource_t _prevsrc;
        bool needsinit = true;

        catacurses::window _w;
        std::shared_ptr<ui_adaptor> _mapui;

        using _slot_rect_entry = std::pair<inclusive_rectangle<point>, slotidx_t>;
        // cache for mouse selection
        std::vector<_slot_rect_entry> mutable _slot_rect_map;

        void _resource();
        void _quick_rebuild();
        void _register_src( slotidx_t c );
        void _ctxt_handler( advuilist<Container, T> *ui, std::string const &action );
        bool _handle_mouse( std::string const &action );
        void _print_map() const;
        icon_t _cycle_slot( slotidx_t idx, icon_t first = 0 );
        bool _set_cycle_slot();
        typename slotcont_t::size_type _countactive( slotidx_t idx ) const;

        // used only for source map window
        static constexpr int const _headersize = 1;
        static constexpr int const _footersize = 1;
        static constexpr int const _firstcol = 1;
        static constexpr int const _iconwidth = 3;
};

// *INDENT-OFF*
template <class Container, typename T>
advuilist_sourced<Container, T>::advuilist_sourced( point const &srclayout, point const &size,
                                                    point const &origin,
                                                    std::string const &ctxtname,
                                                    point const &reserved_rows )
    : advuilist<Container, T>( &_container, size, origin, ctxtname, reserved_rows ), 
      _size( size ),
      _origin( origin ), 
      _map_size( srclayout )
{
// *INDENT-ON*
    using namespace advuilist_literals;

    advuilist<Container, T>::on_input(
        [this]( advuilist<Container, T> *ui, std::string const &action )
    {
        advuilist_sourced<Container, T>::_ctxt_handler( ui, action );
    } );

    advuilist<Container, T>::get_ctxt()->register_action( ACTION_CYCLE_SOURCES );
    advuilist<Container, T>::get_ctxt()->register_action( ACTION_NEXT_SLOT );
    advuilist<Container, T>::get_ctxt()->register_action( ACTION_PREV_SLOT );
    // use a dummy on_resize for base class since we're handling everything in resize()
    advuilist<Container, T>::on_resize( []( advuilist<Container, T> * /**/ ) {} );
    advuilist<Container, T>::on_force_rebuild( [this]( advuilist<Container, T> * /**/ )
    {
        advuilist_sourced<Container, T>::_resource();
    } );
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::add_source( slotidx_t slot, source_t const &src )
{
    icon_t const icon = src.icon;
    auto const it = _sources.find( slot );
    if( it != _sources.end() ) {
        it->second.slotcont[icon] = src;
    } else {
        _sources[slot] = slot_t{ icon, slotcont_t() };
        _sources[slot].slotcont[icon] = src;

        _register_src( slot );
    }
}

template <class Container, typename T>
bool advuilist_sourced<Container, T>::set_source( slotidx_t slotidx, icon_t icon, bool fallthrough,
        bool reb )
{
    auto it = _sources.find( slotidx );
    if( it == _sources.end() ) {
        return false;
    }
    slot_t &slot = it->second;
    slotcont_t const &slotcont = slot.slotcont;
    icon_t target = icon == 0 ? slot.cur_icon : icon;

    auto const target_it = slotcont.find( target );
    if( target_it == slotcont.end() or !target_it->second.source_avail_func() ) {
        // if requested icon is not valid, set the first available one
        target = _cycle_slot( slotidx, slotcont.begin()->first );
    }

    if( target != 0 ) {
        _prevsrc = { _cslot, _sources[_cslot].cur_icon, true };
        slot.cur_icon = target;
        _cslot = slotidx;
        if( reb ) {
            _quick_rebuild();
        }
        if( _mapui ) {
            _mapui->invalidate_ui();
        }
        return true;
    }

    if( fallthrough ) {
        // if we still don't have a valid source on rebuild(), empty the internal container
        _container.clear();
        advuilist<Container, T>::rebuild();
    }

    return false;
}

template <class Container, typename T>
typename advuilist_sourced<Container, T>::getsource_t
advuilist_sourced<Container, T>::get_source() const
{
    icon_t const icon = _sources.at( _cslot ).cur_icon;
    return { _cslot, icon, _sources.at( _cslot ).slotcont.at( icon ).source_avail_func() };
}

template <class Container, typename T>
typename advuilist_sourced<Container, T>::getsource_t
advuilist_sourced<Container, T>::get_source_prev() const
{
    return _prevsrc;
}

template <class Container, typename T>
typename advuilist_sourced<Container, T>::select_t advuilist_sourced<Container, T>::select()
{
    if( !_mapui ) {
        init_ui();
    } else {
        _mapui->invalidate_ui();
    }
    if( needsinit ) {
        rebuild();
    }

    return advuilist<Container, T>::select();
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::rebuild()
{
    needsinit = false;
    set_source( _cslot, 0, true, true );
}

template <class Container, typename T>
std::shared_ptr<ui_adaptor> advuilist_sourced<Container, T>::init_ui()
{
    _mapui = std::make_shared<ui_adaptor>();
    _mapui->on_screen_resize( [&]( ui_adaptor & /* ui */ ) {
        if( _fresize ) {
            _fresize( this );
        } else {
            resize( _osize, _oorigin );
        }
    } );
    _mapui->mark_resize();

    _mapui->on_redraw( [&]( const ui_adaptor & /*ui*/ ) {
        werase( _w );
        draw_border( _w );
        _print_map();
        wmove( _w, _cursor );
        wnoutrefresh( _w );
    } );

    return advuilist<Container, T>::init_ui();
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::hide()
{
    advuilist<Container, T>::hide();
    _mapui.reset();
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::resize( point const &size, point const &origin,
        point const &reserved_rows )
{

    _size = { size.x > 0 ? size.x > TERMX ? TERMX : size.x : TERMX / 4,
              size.y > 0 ? size.y > TERMY ? TERMY : size.y : TERMY / 4
            };
    _origin = { origin.x >= 0 ? origin.x + _size.x > TERMX ? 0 : origin.x : TERMX / 2 - _size.x / 2,
                origin.y >= 0 ? origin.y + _size.y > TERMY ? 0 : origin.y
                : TERMY / 2 - _size.y / 2
              };

    // leave room for source map window
    point const offset( 0, _headersize + _footersize + _map_size.y );
    advuilist<Container, T>::resize( _size - offset, _origin + offset, reserved_rows );

    if( _mapui ) {
        _w = catacurses::newwin( _headersize + _footersize + _map_size.y, _size.x, _origin );
        _mapui->position_from_window( _w );
        _mapui->invalidate_ui();
    }
}

template <class Container, typename T>
std::pair<point, point> advuilist_sourced<Container, T>::get_size() const
{
    return { _size, _origin };
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::on_redraw( fdraw_t const &func )
{
    _fdraw = func;
    advuilist<Container, T>::on_redraw(
    [&]( advuilist<Container, T> * /* ui */ ) {
        _fdraw( this );
    } );
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::on_resize( fdraw_t const &func )
{
    _fresize = func;
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::on_input( fctxt_t const &func )
{
    _fctxt = func;
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::save_state( advuilist_save_state *state ) const
{
    advuilist<Container, T>::save_state( state );
    state->slot = static_cast<uint64_t>( _cslot );
    state->icon = _sources.at( _cslot ).cur_icon;
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::load_state( advuilist_save_state const &state, bool reb )
{
    _cslot = static_cast<slotidx_t>( state.slot );
    set_source( _cslot, state.icon, true, false );

    advuilist<Container, T>::load_state( state, false );

    if( reb ) {
        rebuild();
    }
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::_resource()
{
    // OH DEAR
    _container = _sources[_cslot].slotcont[_sources[_cslot].cur_icon].source_func();
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::_quick_rebuild()
{
    _resource();
    advuilist<Container, T>::rebuild();
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::_register_src( slotidx_t c )
{
    using namespace advuilist_literals;
    advuilist<Container, T>::get_ctxt()->register_action(
        string_format( "%s%d", ACTION_SOURCE_PRFX, c ) );
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::_ctxt_handler( advuilist<Container, T> * /*ui*/,
        std::string const &action )
{
    using namespace advuilist_literals;
    bool reb = false;
    // where is c++20 when you need it?
    if( action.substr( 0, ACTION_SOURCE_PRFX_len ) == ACTION_SOURCE_PRFX ) {
        slotidx_t const slotidx = std::stoul( action.substr( ACTION_SOURCE_PRFX_len, action.size() ) );
        reb = set_source( slotidx, 0, false, false );
    } else if( action == ACTION_CYCLE_SOURCES ) {
        reb = _set_cycle_slot();
    } else if( action == ACTION_NEXT_SLOT ) {
        reb = set_source( _cslot == _sources.size() - 1 ? 0 : _cslot + 1, 0, false, false );
    } else if( action == ACTION_PREV_SLOT ) {
        reb = set_source( _cslot == 0 ? _sources.size() - 1 : _cslot - 1, 0, false, false );
    } else if( action == ACTION_MOUSE_SELECT  or action == ACTION_MOUSE_MOVE ) {
        reb = _handle_mouse( action );
    }

    if( _fctxt ) {
        _fctxt( this, action );
    }

    if( reb ) {
        _quick_rebuild();
    }
}

template <class Container, typename T>
bool advuilist_sourced<Container, T>::_handle_mouse( std::string const &action )
{
    bool reb = false;
    cata::optional<point> const o_p =
        advuilist<Container, T>::get_ctxt()->get_coordinates_text( _w );
    if( o_p and window_contains_point_relative( _w, *o_p ) ) {
        auto const it = std::find_if(
                            _slot_rect_map.cbegin(), _slot_rect_map.cend(),
        [&o_p]( _slot_rect_entry const & i ) {
            return i.first.contains( *o_p );
        } );

        if( it != _slot_rect_map.cend() ) {
            _hslot = it->second;
            if( action == advuilist_literals::ACTION_MOUSE_SELECT ) {
                if( it->second == _cslot ) {
                    reb = _set_cycle_slot();
                } else {
                    reb = set_source( it->second );
                }
            }
        } else {
            _hslot = std::numeric_limits<slotidx_t>::max();
        }

        if( _mapui ) {
            _mapui->invalidate_ui();
        }
    }

    return reb;
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::_print_map() const
{
    // print the name of the current source. we're doing it here instead of down in the loop
    // so that it doesn't cover the source map if it's too long
    icon_t const ci = _sources.at( _cslot ).cur_icon;
    nc_color const bc = c_light_gray;
    std::string const &label = _sources.at( _cslot ).slotcont.at( ci ).label_printer();
    _cursor = { _firstcol, _headersize };
    fold_and_print( _w, _cursor, _size.x, bc, label );

    for( typename srccont_t::value_type const &it : _sources ) {
        slotidx_t const slotidx = it.first;
        slot_t const &slot = it.second;
        icon_t const icon = slot.cur_icon;
        slotcont_t const &slotcont = slot.slotcont;
        source_t const &src = slotcont.at( icon );

        typename slotcont_t::size_type const nactive = _countactive( slotidx );

        nc_color const basecolor = slotidx == _cslot         ? c_white
                                   : src.source_avail_func() ? c_light_gray
                                   : c_red;
        nc_color const color = _hslot == slotidx ? hilite( basecolor ) : basecolor;
        point const loc( slotidx % _map_size.x, slotidx / _map_size.x );
        // visually indicate we have more than one available source in this slot
        std::string const fmt = nactive > 1 ? "<%s>" : "[%s]";
        std::string msg = string_format( fmt, colorize( std::string( 1, icon ), color ) );

        nc_color const fg = _hslot == slotidx ? hilite( c_dark_gray ) : c_dark_gray;
        point const indent( ( _map_size.x - loc.x ) * _iconwidth, loc.y + _headersize );
        int const x = right_print( _w, indent.y, indent.x, fg, msg );
        _slot_rect_map.emplace_back(
        _slot_rect_entry{ { { x, indent.y }, { x + 3, indent.y } }, slotidx } );
    }
}

template <class Container, typename T>
typename advuilist_sourced<Container, T>::icon_t
advuilist_sourced<Container, T>::_cycle_slot( slotidx_t idx, icon_t first )
{
    slot_t const &slot = _sources[idx];
    icon_t const icon = first == 0 ? slot.cur_icon : first;
    slotcont_t const &slotcont = slot.slotcont;

    auto it = slotcont.find( icon );
    // if first is not specified, start with the icon after the currently active one
    if( it != slotcont.end() and first == 0 ) {
        ++it;
    }
    auto const pred = []( typename slotcont_t::value_type const & i ) {
        return i.second.source_avail_func();
    };
    auto find_it = std::find_if( it, slotcont.end(), pred );
    if( find_it != slotcont.end() ) {
        return find_it->first;
    }
    find_it = std::find_if( slotcont.begin(), it, pred );
    if( find_it != it ) {
        return find_it->first;
    }

    return 0;
}

template <class Container, typename T>
bool advuilist_sourced<Container, T>::_set_cycle_slot()
{
    icon_t const next = _cycle_slot( _cslot );
    if( next != 0 ) {
        return set_source( _cslot, next, false, false );
    }
    return false;
}

template <class Container, typename T>
typename advuilist_sourced<Container, T>::slotcont_t::size_type
advuilist_sourced<Container, T>::_countactive( slotidx_t idx ) const
{
    slotcont_t const &slotcont = _sources.at( idx ).slotcont;
    return std::count_if(
               slotcont.begin(), slotcont.end(),
    []( typename slotcont_t::value_type const & it ) {
        return it.second.source_avail_func();
    } );
}

#endif // CATA_SRC_ADVUILIST_SOURCED_H
