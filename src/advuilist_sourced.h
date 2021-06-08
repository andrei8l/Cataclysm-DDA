#ifndef CATA_SRC_ADVUILIST_SOURCED_H
#define CATA_SRC_ADVUILIST_SOURCED_H

#include <cstddef>     // for size_t
#include <cstdint>     // for uint64_t
#include <functional>  // for function
#include <map>         // for map
#include <memory>      // for shared_ptr, __shared_ptr_access
#include <string>      // for string, basic_string, operator==
#include <type_traits> // for declval
#include <utility>     // for pair

#include "advuilist.h"        // for advuilist
#include "advuilist_const.h"  // for ACTION_*
#include "color.h"            // for color_manager, c_dark_gray, c_ligh...
#include "cursesdef.h"        // for newwin, werase, wnoutrefresh, window
#include "output.h"           // for draw_border, right_print, TERMX
#include "point.h"            // for point
#include "string_formatter.h" // for string_format
#include "ui_manager.h"       // for ui_adaptor
#include "uistate.h"          // for advuilist_save_state

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
        explicit advuilist_sourced( point const &srclayout, point size = { -1, -1 }, point origin = { -1, -1 },
                                    std::string const &ctxtname = advuilist_literals::CTXT_DEFAULT,
                                    point reserved_rows = { 2, 1 } );

        /// binds a new source to slot
        void addSource( slotidx_t slot, source_t const &src );
        /// sets active source slot
        ///@param slot
        ///@param icon
        ///@param fallthrough used internally by rebuild() to ensure that the internal list is valid
        ///@param reb if true, also runs advuilist::rebuild(). used internally by loadstate()
        bool setSource( slotidx_t slot, icon_t icon = 0, bool fallthrough = false, bool reb = true );
        getsource_t getSource() const;
        getsource_t getSourcePrev() const;

        select_t select();
        void rebuild();
        void initui();
        void hide();
        void resize( point size, point origin, point reserved_rows = {-1, -1} ); // NOLINT(cata-use-named-point-constants)
        void on_redraw( fdraw_t const &func );
        void on_resize( fdraw_t const &func );

        void setctxthandler( fctxt_t const &func );

        void savestate( advuilist_save_state *state ) const;
        void loadstate( advuilist_save_state const &state, bool reb = true );

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
        getsource_t _prevsrc;
        bool needsinit = true;

        catacurses::window _w;
        std::shared_ptr<ui_adaptor> _mapui;

        void _quick_rebuild();
        void _registerSrc( slotidx_t c );
        void _ctxthandler( advuilist<Container, T> *ui, std::string const &action );
        void _printmap() const;
        icon_t _cycleslot( slotidx_t idx, icon_t first = 0 );
        typename slotcont_t::size_type _countactive( slotidx_t idx ) const;

        // used only for source map window
        static constexpr int const _headersize = 1;
        static constexpr int const _footersize = 1;
        static constexpr int const _firstcol = 1;
        static constexpr int const _iconwidth = 3;
};

// *INDENT-OFF*
template <class Container, typename T>
advuilist_sourced<Container, T>::advuilist_sourced( point const &srclayout, point size,
                                                    point origin, std::string const &ctxtname,
                                                    point reserved_rows )
    : advuilist<Container, T>( &_container, size, origin, ctxtname, reserved_rows ), 
      _size( size ),
      _origin( origin ), 
      _map_size( srclayout )
// *INDENT-ON*

{
    using namespace advuilist_literals;

    advuilist<Container, T>::setctxthandler(
        [this]( advuilist<Container, T> *ui, std::string const & action )
    {
        advuilist_sourced<Container, T>::_ctxthandler( ui, action );
    } );

    advuilist<Container, T>::get_ctxt()->register_action( ACTION_CYCLE_SOURCES );
    advuilist<Container, T>::get_ctxt()->register_action( ACTION_NEXT_SLOT );
    advuilist<Container, T>::get_ctxt()->register_action( ACTION_PREV_SLOT );
    // use a dummy on_resize for base class since we're handling everything in resize()
    advuilist<Container, T>::on_resize( []( advuilist<Container, T> * /**/ ) {} );
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::addSource( slotidx_t slot, source_t const &src )
{
    icon_t const icon = src.icon;
    auto const it = _sources.find( slot );
    if( it != _sources.end() ) {
        it->second.slotcont[icon] = src;
    } else {
        // FIXME: figure out emplace for this
        _sources[slot] = slot_t{ icon, slotcont_t() };
        _sources[slot].slotcont[icon] = src;

        _registerSrc( slot );
    }
}

template <class Container, typename T>
bool advuilist_sourced<Container, T>::setSource( slotidx_t slot, icon_t icon, bool fallthrough,
        bool reb )
{
    slot_t &_slot = _sources[slot];
    slotcont_t &slotcont = _slot.slotcont;
    icon_t const _icon = icon == 0 ? _slot.cur_icon : icon;

    source_t const &src = slotcont[_icon];
    if( src.source_avail_func() ) {
        _prevsrc = { _cslot, _sources[_cslot].cur_icon, true };
        _slot.cur_icon = _icon;
        _cslot = slot;
        if( reb ) {
            _quick_rebuild();
        }
        if( _mapui ) {
            _mapui->invalidate_ui();
        }
        return true;
    }

    // if requested icon is not valid, set the first available one
    icon_t const next = _cycleslot( slot, slotcont.begin()->first );
    if( next != 0 ) {
        return setSource( slot, next, fallthrough, reb );
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
advuilist_sourced<Container, T>::getSource() const
{
    icon_t const icon = _sources.at( _cslot ).cur_icon;
    return { _cslot, icon, _sources.at( _cslot ).slotcont.at( icon ).source_avail_func() };
}

template <class Container, typename T>
typename advuilist_sourced<Container, T>::getsource_t
advuilist_sourced<Container, T>::getSourcePrev() const
{
    return _prevsrc;
}

template <class Container, typename T>
typename advuilist_sourced<Container, T>::select_t advuilist_sourced<Container, T>::select()
{
    if( !_mapui ) {
        initui();
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
    setSource( _cslot, 0, true, true );
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::initui()
{
    _mapui = std::make_shared<ui_adaptor>();
    _mapui->on_screen_resize( [&]( ui_adaptor & ui ) {
        if( _fresize ) {
            _fresize( this );
        } else {
            resize( _osize, _oorigin );
        }
        _w = catacurses::newwin( _headersize + _footersize + _map_size.y, _size.x, _origin );
        ui.position_from_window( _w );
    } );
    _mapui->mark_resize();

    _mapui->on_redraw( [&]( const ui_adaptor & /*ui*/ ) {
        werase( _w );
        draw_border( _w );
        _printmap();
        wmove( _w, _cursor );
        wnoutrefresh( _w );
    } );

    advuilist<Container, T>::initui();
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::hide()
{
    advuilist<Container, T>::hide();
    _mapui.reset();
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::resize( point size, point origin, point reserved_rows )
{

    _size = { size.x > 0 ? size.x > TERMX ? TERMX : size.x : TERMX / 4,
              size.y > 0 ? size.y > TERMY ? TERMY : size.y : TERMY / 4
            };
    _origin = { origin.x >= 0 ? origin.x + size.x > TERMX ? 0 : origin.x : TERMX / 2 - _size.x / 2,
                origin.y >= 0 ? origin.y + size.y > TERMY ? 0 : origin.y
                : TERMY / 2 - _size.y / 2
              };

    // leave room for source map window
    point const offset( 0, _headersize + _footersize + _map_size.y );
    advuilist<Container, T>::resize( _size - offset, _origin + offset, reserved_rows );
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
void advuilist_sourced<Container, T>::setctxthandler( fctxt_t const &func )
{
    _fctxt = func;
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::savestate( advuilist_save_state *state ) const
{
    advuilist<Container, T>::savestate( state );
    state->slot = static_cast<uint64_t>( _cslot );
    state->icon = _sources.at( _cslot ).cur_icon;
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::loadstate( advuilist_save_state const &state, bool reb )
{
    _cslot = static_cast<slotidx_t>( state.slot );
    setSource( _cslot, state.icon, true, false );

    advuilist<Container, T>::loadstate( state, false );

    if( reb ) {
        rebuild();
    }
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::_quick_rebuild()
{
    // OH DEAR
    _container = _sources[_cslot].slotcont[_sources[_cslot].cur_icon].source_func();
    advuilist<Container, T>::rebuild();
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::_registerSrc( slotidx_t c )
{
    using namespace advuilist_literals;
    advuilist<Container, T>::get_ctxt()->register_action(
        string_format( "%s%d", ACTION_SOURCE_PRFX, c ) );
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::_ctxthandler( advuilist<Container, T> * /*ui*/,
        std::string const &action )
{
    using namespace advuilist_literals;
    bool reb = false;
    // where is c++20 when you need it?
    if( action.substr( 0, ACTION_SOURCE_PRFX_len ) == ACTION_SOURCE_PRFX ) {
        slotidx_t const slotidx = std::stoul( action.substr( ACTION_SOURCE_PRFX_len, action.size() ) );
        reb = setSource( slotidx, 0, false, false );
    } else if( action == ACTION_CYCLE_SOURCES ) {
        icon_t const next = _cycleslot( _cslot );
        if( next != 0 ) {
            reb = setSource( _cslot, next, false, false );
        }
    } else if( action == ACTION_NEXT_SLOT ) {
        reb = setSource( _cslot == _sources.size() - 1 ? 0 : _cslot + 1, 0, false, false );
    } else if( action == ACTION_PREV_SLOT ) {
        reb = setSource( _cslot == 0 ? _sources.size() - 1 : _cslot - 1, 0, false, false );
    }

    if( _fctxt ) {
        _fctxt( this, action );
    }

    if( reb ) {
        _quick_rebuild();
    }
}

template <class Container, typename T>
void advuilist_sourced<Container, T>::_printmap() const
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

        // FIXME: maybe cache this?
        typename slotcont_t::size_type const nactive = _countactive( slotidx );

        std::string const color =
            string_format( "<color_%s>", slotidx == _cslot         ? "white"
                           : src.source_avail_func() ? "light_gray"
                           : "red" );
        point const loc( slotidx % _map_size.x, slotidx / _map_size.x );
        // visually indicate we have more than one available source in this slot
        std::string const fmt = nactive > 1 ? "<%s%c</color>>" : "[%s%c</color>]";
        std::string const msg = string_format( fmt, color, icon );

        right_print( _w, loc.y + _headersize, ( _map_size.x - loc.x ) * _iconwidth, c_dark_gray,
                     msg );
    }
}

template <class Container, typename T>
typename advuilist_sourced<Container, T>::icon_t
advuilist_sourced<Container, T>::_cycleslot( slotidx_t idx, icon_t first )
{
    slot_t &slot = _sources[idx];
    icon_t const icon = first == 0 ? slot.cur_icon : first;
    slotcont_t &slotcont = slot.slotcont;

    auto it = slotcont.find( icon );
    // start with the icon after the currently active one, unless we've rolled over
    if( first == 0 ) {
        ++it;
    }
    for( ; it != slotcont.end(); ++it ) {
        if( it->second.source_avail_func() ) {
            return it->first;
        }
    }

    // if we haven't found a good source, roll over to the beginning and try again
    icon_t const _first = slotcont.begin()->first;
    return first != _first ? _cycleslot( idx, _first ) : 0;
}

template <class Container, typename T>
typename advuilist_sourced<Container, T>::slotcont_t::size_type
advuilist_sourced<Container, T>::_countactive( slotidx_t idx ) const
{
    typename slotcont_t::size_type nactive = 0;
    for( auto const &it : _sources.at( idx ).slotcont ) {
        nactive += static_cast<typename slotcont_t::size_type>( it.second.source_avail_func() );
    }

    return nactive;
}

#endif // CATA_SRC_ADVUILIST_SOURCED_H
