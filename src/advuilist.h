#ifndef CATA_SRC_ADVUILIST_H
#define CATA_SRC_ADVUILIST_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "advuilist_const.h"
#include "cata_assert.h"
#include "cata_utility.h"
#include "color.h"
#include "cursesdef.h"
#include "input.h"
#include "localized_comparator.h"
#include "output.h"
#include "point.h"
#include "sdltiles.h"
#include "string_formatter.h"
#include "string_input_popup.h"
#include "translations.h"
#include "ui.h"
#include "ui_manager.h"

class JsonObject;
class JsonOut;

struct advuilist_save_state {
    public:
        uint64_t slot = 0, idx = 0, sort = 0, group = 0;
        char icon = 0;
        std::string filter;
        bool initialized = false;

        void serialize( JsonOut &json ) const;
        void deserialize( JsonObject const &jo );
};

template <class Container, typename T = typename Container::value_type>
class advuilist
{
    public:
        /// column printer function. entry, colwidth. colwidth = 0 means unlimited
        using fcol_t = std::function<std::string( T const &, int )>;
        using cwidth_t = int;
        struct col_t {
            std::string name;
            fcol_t printer;
            cwidth_t width = 0;
        };
        using count_t = std::size_t;
        /// counting function. used only for partial/whole selection
        using fcounter_t = std::function<count_t( T const & )>;
        /// on_rebuild function. params are reset, element
        using frebuild_t = std::function<void( bool, T const & )>;
        using fdraw_t = std::function<void( advuilist<Container, T> * )>;
        /// sorting function
        using fsort_t = std::function<bool( T const &, T const & )>;
        struct sorter_t {
            std::string name;
            fsort_t sorter;
        };
        /// filter function. params are element, filter string
        using ffilter_t = std::function<bool( T const &, std::string const & )>;
        struct filter_t {
            std::string desc;
            ffilter_t filter_func;
        };
        /// ctxt handler function for adding extra functionality
        using fctxt_t = std::function<void( advuilist<Container, T> *, std::string const & )>;
        /// group label printer
        using fglabel_t = std::function<std::string( T const & )>;
        struct grouper_t {
            std::string name;
            fsort_t sorter;
            fglabel_t fgid;
        };

        using ptr_t = typename Container::iterator;
        /// count, pointer. count is always > 0
        struct selection_t {
            count_t count;
            ptr_t ptr;
        };
        using select_t = std::vector<selection_t>;

        // NOLINTNEXTLINE(cata-use-named-point-constants)
        explicit advuilist( Container *list, point const &size = { -1, -1 },
                            point const &origin = { -1, -1 },
                            std::string const &ctxtname = advuilist_literals::CTXT_DEFAULT,
                            point const &reserved_rows = { 2, 1 } );

        /// sets up columns and optionally sets up implict sorters
        ///@param columns
        ///@param implicit if true, implicit sorters will be created for each column. will replace sorters
        ///                with same name (see addSorter )
        void set_columns( std::vector<col_t> const &columns, bool implicit = true );
        /// adds a new sorter. replaces existing sorter with same name (including implicit column
        /// sorters)
        ///@param sorter
        void add_sorter( sorter_t const &sorter );
        /// adds a new grouper. replaces existing grouper with same name
        ///@param grouper
        void add_grouper( grouper_t const &grouper );
        /// sets a handler for input_context actions. this is executed after all internal actions are
        /// handled
        void on_input( fctxt_t const &func );
        /// sets the element counting function. enables partial and whole selection
        void set_fcounting( fcounter_t const &func );
        /// if set, func gets called for every element that gets added to the internal list. This is
        /// meant to be used for collecting stats.
        /// Passed args are either (true, null element) or (false, element)
        void on_rebuild( frebuild_t const &func );
        void on_force_rebuild( fdraw_t const &func );
        /// if set, func gets called after all internal printing calls and before wnoutrefresh(). This
        /// is meant to be used for drawing extra decorations or stats
        void on_redraw( fdraw_t const &func );
        /// if set, func will get called when filtering is started. Use this to print instructions
        /// in a non-standard place (outside of the popup window)
        void on_filter( fdraw_t const &func );
        /// if set, func will get called on screen resize instead of running resize() directly. Use
        /// this if window size/pos depends on external variables such as TERMX/Y
        void on_resize( fdraw_t const &func );
        /// sets the filtering function
        void set_ffilter( filter_t const &func );
        select_t select();
        void sort( std::string const &name );
        void rebuild( Container *list = nullptr );
        /// returns the currently hilighted element. meant to be called from the external ctxt handler
        /// added by setctxthandler()
        select_t peek() const;
        /// breaks internal loop in select(). meant to be called from the external ctxt handler added by
        /// setctxthandler()
        void suspend();
        /// pre-initialize or reset the internal ui_adaptor;
        std::shared_ptr<ui_adaptor> init_ui();
        void hide();
        // NOLINTNEXTLINE(cata-use-named-point-constants)
        void resize( point const &size, point const &origin, point const &reserved_rows = {-1, -1} );
        void force_rebuild( bool state );

        input_context *get_ctxt();
        catacurses::window *get_window();
        std::shared_ptr<ui_adaptor> get_ui();
        std::pair<point, point> get_size() const;

        void save_state( advuilist_save_state *state ) const;
        void load_state( advuilist_save_state const &state, bool reb = true );

    private:
        /// pair of index, pointer. index is used for "none" sorting mode and is not meant to represent
        /// index in the original Container (which may not even be indexable)
        using nidx_t = typename Container::size_type;
        struct entry_t {
            nidx_t idx;
            ptr_t ptr;
        };
        using list_t = std::vector<entry_t>;
        using colscont_t = std::vector<col_t>;
        using sortcont_t = std::vector<sorter_t>;
        /// pages are pairs of direct indices of the internal list
        using page_t = std::pair<typename list_t::size_type, typename list_t::size_type>;
        using groupercont_t = std::vector<grouper_t>;
        using pagecont_t = std::vector<page_t>;

        point _size, _osize;
        point _origin, _oorigin;
        point mutable _cursor;
        typename list_t::size_type _pagesize = 1;
        list_t _list;
        colscont_t _columns;
        sortcont_t _sorters;
        groupercont_t _groupers;
        pagecont_t _pages;
        ffilter_t _ffilter;
        fcounter_t _fcounter;
        frebuild_t _frebuild;
        fdraw_t _fdraw, _fresize, _fdraw_filter, _ffrebuild;
        fctxt_t _fctxt;
        std::string _filter;
        std::string _filterdsc;
        typename sortcont_t::size_type _csort = 0;
        typename groupercont_t::size_type _cgroup = 0;
        typename list_t::size_type _cidx = 0;
        typename pagecont_t::size_type _cpage = 0;
        /// total column width weights
        cwidth_t _tweight = 0;
        int _innerw = 0;
        /// number of lines at the top of the window reserved for decorations
        int _headersize = 2;
        /// number of lines at the bottom of the window reserved for decorations
        int _footersize = 1;
        bool _exit = true;
        bool _needsinit = true;
        using _idx_line_entry = std::pair<int, typename list_t::size_type>;
        // cache for mouse selection
        std::vector<_idx_line_entry> mutable _idx_line_map;
        bool _force_rebuild = false;

        input_context _ctxt;
        catacurses::window _w;
        std::shared_ptr<ui_adaptor> _ui;

        Container *_olist = nullptr;

        /// first usable column after decorations
        static constexpr int const _firstcol = 1;
        /// minimum whitespace between columns
        static constexpr int const _colspacing = 1;

        select_t _peek( count_t amount ) const;
        select_t _peekall() const;
        count_t _count( typename list_t::size_type idx ) const;
        count_t _peekcount() const;

        template <class add_container, typename C = typename add_container::value_type>
        void _add_common( add_container *cont, C const &newc );
        void _initctxt();
        void _print() const;
        int _colwidth( col_t const &col, point const &p ) const;
        int _printcol( col_t const &col, std::string const &str, point const &p,
                       nc_color const &color ) const;
        int _printcol( col_t const &col, T const &it, point const &p, nc_color const &color,
                       bool hilited ) const;
        void _printheaders() const;
        void _printfooters() const;
        void _sort( typename sortcont_t::size_type sidx, typename groupercont_t::size_type gidx );
        void _paginate();
        void _querysort();
        void _queryfilter();
        count_t _querypartial();
        void _setfilter( std::string const &filter );
        bool _basicfilter( T const &it, std::string const &filter ) const;
        typename pagecont_t::size_type _idxtopage( typename list_t::size_type idx ) const;
        void _incidx( typename list_t::size_type amount );
        void _decidx( typename list_t::size_type amount );
        void _setidx( typename list_t::size_type idx );
};

// *INDENT-OFF*
template <class Container, typename T>
advuilist<Container, T>::advuilist( Container *list, point const &size, point const &origin,
                                    std::string const &ctxtname, point const &reserved_rows )
    : _osize( size ),
      _oorigin( origin ),
      // insert dummy sorter for "none" sort mode
      _sorters{ sorter_t{ "none", fsort_t() } },
      // insert dummy grouper for "none" grouping mode
      _groupers{ grouper_t{ "none", fsort_t(), fglabel_t() } },
      // use the basic filter if one isn't supplied
      _ffilter{ [this]( T const &it, std::string const &filter ) {
          return this->_basicfilter( it, filter );
      } },
      _innerw( _size.x - _firstcol * 2 ),
      _headersize( reserved_rows.x ),
      _footersize( reserved_rows.y ),
      _ctxt( ctxtname ),
      // remember constructor list so we can rebuild internal list when filtering
      _olist( list )
{
// *INDENT-ON*
    _initctxt();
}

template <class Container, typename T>
void advuilist<Container, T>::set_columns( std::vector<col_t> const &columns, bool implicit )
{
    _columns.clear();
    _tweight = 0;

    sortcont_t tmp;
    for( col_t const &v : columns ) {
        _columns.emplace_back( v );
        _tweight += v.width;

        // build new implicit column sorters
        if( implicit ) {
            typename colscont_t::size_type const idx = _columns.size() - 1;
            tmp.emplace_back( sorter_t{ v.name, [this, idx]( T const & lhs, T const & rhs ) -> bool {
                    return localized_compare(
                        _columns[idx].printer( lhs, 0 ),
                        _columns[idx].printer( rhs, 0 ) );
                } } );
        }
    }

    for( sorter_t const &s : tmp ) {
        add_sorter( s );
    }
}

template <class Container, typename T>
template <class add_container, typename C>
void advuilist<Container, T>::_add_common( add_container *cont, C const &newc )
{
    auto it = std::find_if( cont->begin(), cont->end(), [&]( C const & v ) {
        return v.name == newc.name;
    } );
    if( it != cont->end() ) {
        *it = newc;
    } else {
        cont->emplace_back( newc );
    }
}

template <class Container, typename T>
void advuilist<Container, T>::add_sorter( sorter_t const &sorter )
{
    _add_common( &_sorters, sorter );
}

template <class Container, typename T>
void advuilist<Container, T>::add_grouper( grouper_t const &grouper )
{
    _add_common( &_groupers, grouper );
}

template <class Container, typename T>
void advuilist<Container, T>::on_input( fctxt_t const &func )
{
    _fctxt = func;
}

template <class Container, typename T>
void advuilist<Container, T>::set_fcounting( fcounter_t const &func )
{
    _fcounter = func;
}

template <class Container, typename T>
void advuilist<Container, T>::on_rebuild( frebuild_t const &func )
{
    _frebuild = func;
}

template <class Container, typename T>
void advuilist<Container, T>::on_force_rebuild( fdraw_t const &func )
{
    _ffrebuild = func;
}

template <class Container, typename T>
void advuilist<Container, T>::on_redraw( fdraw_t const &func )
{
    _fdraw = func;
}

template <class Container, typename T>
void advuilist<Container, T>::on_filter( fdraw_t const &func )
{
    _fdraw_filter = func;
}

template <class Container, typename T>
void advuilist<Container, T>::on_resize( fdraw_t const &func )
{
    _fresize = func;
}

template <class Container, typename T>
void advuilist<Container, T>::set_ffilter( filter_t const &func )
{
    _filterdsc = func.desc;
    _ffilter = func.filter_func;
}

template <class Container, typename T>
typename advuilist<Container, T>::select_t advuilist<Container, T>::select()
{
    using namespace advuilist_literals;
    // reset exit variable in case suspend() was called earlier
    _exit = false;

    if( !_ui ) {
        init_ui();
    }
    if( _needsinit ) {
        rebuild();
    }

    while( !_exit ) {

        _ui->invalidate_ui();
        ui_manager::redraw_invalidated();
        std::string const action = _ctxt.handle_input();

        if( action == ACTION_UP ) {
            _decidx( 1 );
        } else if( action == ACTION_DOWN ) {
            _incidx( 1 );
        } else if( action == ACTION_MOUSE_SELECT  or action == ACTION_MOUSE_MOVE ) {
            cata::optional<point> const o_p = _ctxt.get_coordinates_text( _w );
            if( o_p and window_contains_point_relative( _w, *o_p ) ) {
                auto const it = std::find_if(
                                    _idx_line_map.cbegin(), _idx_line_map.cend(),
                [&o_p]( _idx_line_entry const & i ) {
                    return i.first == o_p->y;
                } );

                if( it != _idx_line_map.cend() ) {
                    if( action == ACTION_MOUSE_SELECT and it->second == _cidx ) {
                        return peek();
                    }
                    _setidx( it->second );
                }
            }
        } else if( action == ACTION_PAGE_UP or action == ACTION_SCROLL_UP ) {
            _decidx( _pagesize );
        } else if( action == ACTION_PAGE_DOWN or action == ACTION_SCROLL_DOWN ) {
            _incidx( _pagesize );
        } else if( action == ACTION_SORT ) {
            _querysort();
        } else if( action == ACTION_FILTER ) {
            _queryfilter();
        } else if( action == ACTION_RESET_FILTER ) {
            _setfilter( std::string() );
        } else if( action == ACTION_SELECT ) {
            return peek();
        } else if( action == ACTION_SELECT_PARTIAL ) {
            if( !_list.empty() ) {
                count_t const input = _querypartial();
                if( input > 0 ) {
                    return _peek( input );
                }
            }
        } else if( action == ACTION_SELECT_WHOLE ) {
            return _peek( _peekcount() );
        } else if( action == ACTION_SELECT_ALL ) {
            return _peekall();
        } else if( action == ACTION_QUIT ) {
            _exit = true;
        }

        if( _fctxt ) {
            _fctxt( this, action );
        }
    }

    return {};
}

template <class Container, typename T>
void advuilist<Container, T>::sort( std::string const &name )
{
    auto const it = std::find_if( _sorters.begin(), _sorters.end(), [&]( sorter_t const & v ) {
        return v.name == name;
    } );
    if( it != _sorters.end() ) {
        _sort(
            static_cast<typename sortcont_t::size_type>( std::distance( _sorters.begin(), it ) ), _cgroup );
    }
}

template <class Container, typename T>
void advuilist<Container, T>::rebuild( Container *list )
{
    _list.clear();
    Container *rlist = list == nullptr ? _olist : list;
    nidx_t idx = 0;
    if( _force_rebuild && _ffrebuild ) {
        _ffrebuild( this );
    }
    if( _frebuild ) {
        static T const nullentry;
        _frebuild( true, nullentry );
    }
    for( auto it = rlist->begin(); it != rlist->end(); ++it ) {
        if( !_ffilter or _filter.empty() or _ffilter( *it, _filter ) ) {
            if( _frebuild ) {
                _frebuild( false, *it );
            }
            _list.emplace_back( entry_t{ idx++, it } );
        }
    }
    _sort( _csort, _cgroup );
    _paginate();
    _setidx( _cidx );
    _needsinit = false;
}

template <class Container, typename T>
typename advuilist<Container, T>::select_t advuilist<Container, T>::peek() const
{
    return _peek( 1 );
}

template <class Container, typename T>
void advuilist<Container, T>::suspend()
{
    _exit = true;
    // redraw darker borders, etc
    _ui->invalidate_ui();
}

template <class Container, typename T>
std::shared_ptr<ui_adaptor> advuilist<Container, T>::init_ui()
{
    _ui = std::make_shared<ui_adaptor>();
    _ui->on_screen_resize( [&]( ui_adaptor & /* ui */ ) {
        if( _fresize ) {
            _fresize( this );
        } else {
            resize( _osize, _oorigin );
        }
    } );
    _ui->mark_resize();

    _ui->on_redraw( [&]( const ui_adaptor & /*ui*/ ) {
        werase( _w );
        if( _force_rebuild ) {
            rebuild();
        }
        draw_border( _w, _exit ? c_dark_gray : c_light_gray );
        _print();
        if( _fdraw ) {
            _fdraw( this );
        }
        wmove( _w, _cursor );
        wnoutrefresh( _w );
    } );

    return _ui;
}

template <class Container, typename T>
void advuilist<Container, T>::hide()
{
    _ui.reset();
}

template <class Container, typename T>
void advuilist<Container, T>::resize( point const &size, point const &origin,
                                      point const &reserved_rows )
{
    _size = { size.x > 0 ? size.x > TERMX ? TERMX : size.x : TERMX / 4,
              size.y > 0 ? size.y > TERMY ? TERMY : size.y : TERMY / 4
            };
    _origin = { origin.x >= 0 ? origin.x + _size.x > TERMX ? 0 : origin.x : TERMX / 2 - _size.x / 2,
                origin.y >= 0 ? origin.y + _size.y > TERMY ? 0 : origin.y
                : TERMY / 2 - _size.y / 2
              };
    _headersize = reserved_rows.x > 0 ? reserved_rows.x : _headersize;
    _footersize = reserved_rows.y > 0 ? reserved_rows.y : _footersize;

    _innerw = _size.x - _firstcol * 2;
    // leave space for decorations and column headers
    typename list_t::size_type const npagesize = static_cast<typename list_t::size_type>(
                std::max( 1, _size.y - ( _headersize + _footersize + 1 ) ) );
    if( npagesize != _pagesize ) {
        _pagesize = npagesize;
        rebuild( _olist );
    }

    if( _ui ) {
        _w = catacurses::newwin( _size.y, _size.x, _origin );
        _ui->position_from_window( _w );
        _ui->invalidate_ui();
    }
}

template <class Container, typename T>
void advuilist<Container, T>::force_rebuild( bool state )
{
    _force_rebuild = state;
}

template <class Container, typename T>
input_context *advuilist<Container, T>::get_ctxt()
{
    return &_ctxt;
}

template <class Container, typename T>
catacurses::window *advuilist<Container, T>::get_window()
{
    return &_w;
}

template <class Container, typename T>
std::shared_ptr<ui_adaptor> advuilist<Container, T>::get_ui()
{
    return _ui;
}

template <class Container, typename T>
std::pair<point, point> advuilist<Container, T>::get_size() const
{
    return { _size, _origin };
}

template <class Container, typename T>
void advuilist<Container, T>::save_state( advuilist_save_state *state ) const
{
    state->idx = static_cast<uint64_t>( _cidx );
    state->sort = static_cast<uint64_t>( _csort );
    state->group = static_cast<uint64_t>( _cgroup );
    state->filter = _filter;
    state->initialized = true;
}

template <class Container, typename T>
void advuilist<Container, T>::load_state( advuilist_save_state const &state, bool reb )
{
    _csort = static_cast<typename sortcont_t::size_type>( state.sort );
    _cgroup = static_cast<typename groupercont_t::size_type>( state.group );
    _filter = state.filter;
    if( reb ) {
        rebuild();
    } else {
        _setidx( state.idx );
    }
}

template <class Container, typename T>
typename advuilist<Container, T>::select_t advuilist<Container, T>::_peek( count_t amount ) const
{
    if( _list.empty() ) {
        return select_t();
    }

    return select_t{ selection_t{ amount, _list.at( _cidx ).ptr } };
}

template <class Container, typename T>
typename advuilist<Container, T>::select_t advuilist<Container, T>::_peekall() const
{
    select_t ret;
    for( typename list_t::size_type idx = 0; idx < _list.size(); idx++ ) {
        count_t const amount = _count( idx );
        ret.emplace_back( selection_t{ amount, _list[idx].ptr } );
    }

    return ret;
}

template <class Container, typename T>
typename advuilist<Container, T>::count_t
advuilist<Container, T>::_count( typename list_t::size_type idx ) const
{
    if( _list.empty() ) {
        return 0;
    }
    if( _fcounter ) {
        return _fcounter( *_list.at( idx ).ptr );
    }
    return 1;
}

template <class Container, typename T>
typename advuilist<Container, T>::count_t advuilist<Container, T>::_peekcount() const
{
    return _count( _cidx );
}

template <class Container, typename T>
void advuilist<Container, T>::_initctxt()
{
    using namespace advuilist_literals;
    _ctxt.register_updown();
    _ctxt.register_action( ACTION_FILTER );
    _ctxt.register_action( ACTION_HELP_KEYBINDINGS );
    _ctxt.register_action( ACTION_MOUSE_MOVE );
    _ctxt.register_action( ACTION_MOUSE_SELECT );
    _ctxt.register_action( ACTION_PAGE_DOWN );
    _ctxt.register_action( ACTION_PAGE_UP );
    _ctxt.register_action( ACTION_QUIT );
    _ctxt.register_action( ACTION_RESET_FILTER );
    _ctxt.register_action( ACTION_SCROLL_DOWN );
    _ctxt.register_action( ACTION_SCROLL_UP );
    _ctxt.register_action( ACTION_SELECT );
    _ctxt.register_action( ACTION_SELECT_ALL );
    _ctxt.register_action( ACTION_SELECT_PARTIAL );
    _ctxt.register_action( ACTION_SELECT_WHOLE );
    _ctxt.register_action( ACTION_SORT );
}

template <class Container, typename T>
void advuilist<Container, T>::_print() const
{
    _printheaders();
    _idx_line_map.clear();

    if( _force_rebuild ) {
        right_print( _w, 0, 0, c_light_red, "*" );
    }

    point lpos( _firstcol, _headersize );

    nc_color const colcolor = _exit ? c_light_gray : c_white;
    // print column headers
    for( col_t const &col : _columns ) {
        lpos.x += _printcol( col, col.name, lpos, colcolor );
    }
    lpos.y += 1;

    // print entries
    typename list_t::size_type const cpagebegin = _pages[_cpage].first;
    typename list_t::size_type const cpageend = _pages[_cpage].second;
    std::string cgroup;
    fglabel_t const &fglabel = _groupers[_cgroup].fgid;
    for( typename list_t::size_type idx = cpagebegin; idx < cpageend; idx++ ) {
        T const &it = *_list[idx].ptr;

        // print group header if appropriate
        if( _cgroup != 0 ) {
            std::string const &ngroup = fglabel( it );
            if( ngroup != cgroup ) {
                cgroup = ngroup;
                center_print( _w, lpos.y, c_cyan,
                              string_format( "[%s]", fglabel( it ) ) );
                lpos.y += 1;
            }
        }

        lpos.x = _firstcol;
        nc_color const basecolor = _exit ? c_dark_gray : c_light_gray;
        bool const hilited = idx == _cidx and !_exit;
        nc_color const color = hilited ? hilite( basecolor ) : basecolor;

        if( hilited ) {
            _cursor = lpos;
            mvwprintz( _w, lpos, color, string_format( "%*s", _innerw, std::string() ) );
        }

        for( col_t const &col : _columns ) {
            lpos.x += _printcol( col, it, lpos, color, hilited );
        }
        _idx_line_map.emplace_back( _idx_line_entry{ lpos.y, idx } );
        lpos.y += 1;
    }

    _printfooters();
}

template <class Container, typename T>
int advuilist<Container, T>::_colwidth( col_t const &col, point const &p ) const
{
    int const colwidth = std::min(
                             _innerw - p.x,
                             static_cast<int>( std::ceil( static_cast<float>( col.width * _innerw ) / _tweight ) ) );
    bool const last = p.x + colwidth < _innerw;
    return colwidth - ( last ? _colspacing : 0 );
}

template <class Container, typename T>
int advuilist<Container, T>::_printcol( col_t const &col, std::string const &str, point const &p,
                                        nc_color const &color ) const
{
    int const colwidth = _colwidth( col, p );
    trim_and_print( _w, p, colwidth, color, str );
    return colwidth + _colspacing;
}

template <class Container, typename T>
int advuilist<Container, T>::_printcol( col_t const &col, T const &it, point const &p,
                                        nc_color const &color, bool hilited ) const
{
    int const colwidth = _colwidth( col, p );
    std::string const &rawmsg = col.printer( it, colwidth );
    std::string const &msg = hilited ? remove_color_tags( rawmsg ) : rawmsg;
    trim_and_print( _w, p, colwidth, color, msg );
    return colwidth + _colspacing;
}

template <class Container, typename T>
void advuilist<Container, T>::_printheaders() const
{
    using namespace advuilist_literals;
    // sort mode
    mvwprintw( _w, { _firstcol, 0 }, _( "< [%s] Sort: %s >" ), _ctxt.get_desc( ACTION_SORT ),
               _sorters[_csort].name );

    // page index
    typename pagecont_t::size_type const cpage = _cpage + 1;
    typename pagecont_t::size_type const npages = _pages.size();
    std::string const msg2 = string_format( _( "[<] page %1$d of %2$d [>]" ), cpage, npages );
    trim_and_print( _w, { _firstcol, 1 }, _size.x, c_light_blue, msg2 );

    // keybinding hint
    std::string const msg3 =
        string_format( _( "< [%s] keybindings > " ),
                       colorize( _ctxt.get_desc( ACTION_HELP_KEYBINDINGS ), c_yellow ) );
    right_print( _w, 0, 0, c_white, msg3 );
}

template <class Container, typename T>
void advuilist<Container, T>::_printfooters() const
{
    using namespace advuilist_literals;
    // filter
    std::string const fprefix = string_format( _( "[%s] Filter" ), _ctxt.get_desc( ACTION_FILTER ) );
    if( !_filter.empty() ) {
        mvwprintw( _w, { _firstcol, _size.y - 1 }, "< %s: %s >", fprefix, _filter );
    } else {
        mvwprintw( _w, { _firstcol, _size.y - 1 }, "< %s >", fprefix );
    }
}

template <class Container, typename T>
void advuilist<Container, T>::_sort( typename sortcont_t::size_type sidx,
                                     typename groupercont_t::size_type gidx )
{
    struct cmp {
        private:
            sorter_t const &s;
            grouper_t const &g;

        public:
            explicit constexpr cmp( sorter_t const &_s, grouper_t const &_g ) : s( _s ), g( _g ) {}

            constexpr bool operator()( entry_t const &lhs, entry_t const &rhs ) const {
                if( !g.sorter or !g.fgid or g.fgid( *lhs.ptr ) == g.fgid( *rhs.ptr ) ) {
                    if( s.sorter ) {
                        return s.sorter( *lhs.ptr, *rhs.ptr );
                    }
                    // handle "none" sort mode too
                    return lhs.idx < rhs.idx;
                }
                return g.sorter( *lhs.ptr, *rhs.ptr );
            }
    };

    std::stable_sort( _list.begin(), _list.end(), cmp( _sorters[sidx], _groupers[gidx] ) );
    _csort = sidx;
    _cgroup = gidx;
}

template <class Container, typename T>
void advuilist<Container, T>::_paginate()
{
    _pages.clear();

    // build page index pairs
    auto gbegin = _list.begin();
    typename list_t::size_type pbegin = 0;
    // reserve extra space for the first group header;
    typename list_t::size_type lpagesize = _pagesize - ( _cgroup != 0 ? 1 : 0 );
    if( lpagesize != 0 ) {
        typename list_t::size_type cpentries = 0;
        fglabel_t const &fglabel = _groupers[_cgroup].fgid;
        for( auto it = _list.begin(); it != _list.end(); ++it ) {
            if( fglabel and fglabel( *it->ptr ) != fglabel( *gbegin->ptr ) ) {
                gbegin = it;
                // group header takes up the space of one entry
                cpentries++;
            }

            cpentries++;

            if( cpentries > lpagesize ) {
                cata_assert( cpentries % lpagesize <= 2 );
                typename list_t::size_type const ci = std::distance( _list.begin(), it );
                _pages.emplace_back( pbegin, ci );
                pbegin = ci;
                cpentries = 1;
            }
        }
    }
    if( pbegin < _list.size() or _list.empty() ) {
        _pages.emplace_back( pbegin, _list.size() );
    }
}

template <class Container, typename T>
void advuilist<Container, T>::_querysort()
{
    uilist menu;
    menu.text = _( "Sort by…" );
    int const nsorters = static_cast<int>( _sorters.size() );
    int const ngroupers = static_cast<int>( _groupers.size() );
    for( int i = 0; i < nsorters; i++ ) {
        menu.addentry( i, true, MENU_AUTOASSIGN, _sorters[i].name );
    }
    menu.addentry( nsorters, false, 0, _( "Group by…" ), '-' );
    for( int i = 0; i < ngroupers; i++ ) {
        menu.addentry( nsorters + i, true, MENU_AUTOASSIGN, _groupers[i].name );
    }
    menu.query();
    if( menu.ret >= 0 ) {
        if( menu.ret < nsorters ) {
            _sort( menu.ret, _cgroup );
        } else {
            _sort( _csort, menu.ret - nsorters );
            _paginate();
        }
    }
}

template <class Container, typename T>
void advuilist<Container, T>::_queryfilter()
{
    if( _fdraw_filter ) {
        _fdraw_filter( this );
    }

    string_input_popup spopup;
    spopup.max_length( 256 ).text( _filter );
    spopup.identifier( _ctxt.get_category() );
    if( !_filterdsc.empty() ) {
        spopup.description( _filterdsc );
    } else {
        spopup.window( _w, point( 2, _size.y - 1 ), _size.x - 2 );
    }

    do {
        _ui->invalidate_ui();
        ui_manager::redraw();
        std::string const nfilter = spopup.query_string( false );
        if( !spopup.canceled() and nfilter != _filter ) {
            _setfilter( nfilter );
        }
    } while( !spopup.canceled() and !spopup.confirmed() );
}

template <class Container, typename T>
typename advuilist<Container, T>::count_t advuilist<Container, T>::_querypartial()
{
    count_t const max = _peekcount();
    string_input_popup spopup;
    spopup.title(
        string_format( _( "How many do you want to select?  [Max %d] (0 to cancel)" ), max ) );
    spopup.width( 20 );
    spopup.only_digits( true );
    count_t const amount = spopup.query_int64_t();

    return spopup.canceled() ? 0 : std::min( max, amount );
}

template <class Container, typename T>
void advuilist<Container, T>::_setfilter( std::string const &filter )
{
    _filter = filter;
    rebuild( _olist );
}

template <class Container, typename T>
bool advuilist<Container, T>::_basicfilter( T const &it, std::string const &filter ) const
{
    return std::any_of( _columns.begin(), _columns.end(), [&]( const col_t & col ) {
        return lcmatch( remove_color_tags( col.printer( it, 0 ) ), filter );
    } );
}

template <class Container, typename T>
typename advuilist<Container, T>::pagecont_t::size_type
advuilist<Container, T>::_idxtopage( typename list_t::size_type idx ) const
{
    typename pagecont_t::size_type cpage = 0;
    while( _pages.at( cpage ).second != 0 and _pages.at( cpage ).second <= idx ) {
        cpage++;
    }
    return cpage;
}

template <class Container, typename T>
void advuilist<Container, T>::_incidx( typename list_t::size_type amount )
{
    if( _pages.back().second == 0 ) {
        _cidx = 0;
        _cpage = 0;
        return;
    }
    if( _cidx == _pages.back().second - 1 ) {
        _cidx = _pages.front().first;
    } else {
        _cidx = std::min( _cidx + amount, _pages.back().second - 1 );
    }
    _cpage = _idxtopage( _cidx );
}

template <class Container, typename T>
void advuilist<Container, T>::_decidx( typename list_t::size_type amount )
{
    if( _pages.back().second == 0 ) {
        _cidx = 0;
        _cpage = 0;
        return;
    }
    if( _cidx == _pages.front().first ) {
        _cidx = _pages.back().second - 1;
    } else {
        _cidx = _pages.front().first + amount > _cidx ? _pages.front().first : _cidx - amount;
    }
    _cpage = _idxtopage( _cidx );
}

template <class Container, typename T>
void advuilist<Container, T>::_setidx( typename list_t::size_type idx )
{
    _cidx = _pages.back().second == 0
            ? 0
            : idx > _pages.back().second - 1 ? _pages.back().second - 1 : idx;
    _cpage = _idxtopage( _cidx );
}

#endif // CATA_SRC_ADVUILIST_H
