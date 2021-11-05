#include "advuilist_helpers.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "auto_pickup.h"
#include "avatar.h"
#include "character.h"
#include "color.h"
#include "input.h"
#include "item.h"
#include "item_category.h"
#include "item_contents.h"
#include "item_location.h"
#include "item_pocket.h"
#include "item_search.h"
#include "localized_comparator.h"
#include "map.h"
#include "map_selector.h"
#include "npctrade.h"
#include "optional.h"
#include "output.h"
#include "string_formatter.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"
#include "units_utility.h"
#include "vehicle.h"
#include "vehicle_selector.h"
#include "vpart_position.h"

namespace catacurses
{
class window;
} // namespace catacurses

namespace
{
using namespace advuilist_helpers;

units::mass _iloc_entry_weight( iloc_entry const &it )
{
    units::mass ret = 0_kilogram;
    for( auto const &v : it.stack ) {
        ret += v->weight();
    }
    return ret;
}

units::volume _iloc_entry_volume( iloc_entry const &it )
{
    units::volume ret = 0_liter;
    for( auto const &v : it.stack ) {
        ret += v->volume();
    }
    return ret;
}

using stack_cache_t = std::unordered_map<itype_id, std::set<iloc_stack_t::size_type>>;
void _stack_elem( item *elem, iloc_stack_t *stacks, stack_cache_t *cache,
                  filoc_t const &iloc_helper )
{
    itype_id const id = elem->typeId();
    auto const iter = cache->find( id );
    bool got_stacked = false;
    if( iter != cache->end() ) {
        for( auto const &idx : iter->second ) {
            got_stacked = ( *stacks )[idx].stack.front()->display_stacked_with( *elem );
            if( got_stacked ) {
                ( *stacks )[idx].stack.emplace_back( iloc_helper( elem ) );
                break;
            }
        }
    }
    if( !got_stacked ) {
        ( *cache )[id].insert( stacks->size() );
        stacks->emplace_back( iloc_entry{ { iloc_helper( elem ) } } );
    }
}

void _get_stacks( item *elem, iloc_stack_t *stacks, stack_cache_t *cache,
                  filoc_t const &iloc_helper )
{
    _stack_elem( elem, stacks, cache, iloc_helper );

    if( elem->is_corpse() ) {
        for( item *corpse_elem : elem->all_items_top( item_pocket::pocket_type::CONTAINER ) ) {
            _stack_elem( corpse_elem, stacks, cache,
            [&]( item * it ) {
                return item_location( iloc_helper( elem ), it );
            } );
        }
    }
}

void _append_stacks( Character *guy, item_location i, aim_container_t *ret )
{
    aim_container_t const temp =
        get_stacks( i->all_items_top( item_pocket::pocket_type::CONTAINER ),
    [guy]( item * it ) {
        return item_location( *guy, it );
    } );
    ret->insert( ret->end(), std::make_move_iterator( temp.begin() ),
                 std::make_move_iterator( temp.end() ) );
}

std::string _format_value( double value, int width )
{
    int const digits = static_cast<int>( std::log10( std::max( 1., value ) ) ) + 1;
    int const decimals = std::max( 0, std::min( 2, width - digits - 1 ) );
    return string_format( "%*.*f", width, decimals, value );
}

} // namespace

namespace advuilist_helpers
{

cata::optional<vpart_reference> veh_cargo_at( tripoint const &loc )
{
    return get_map().veh_at( loc ).part_with_feature( "CARGO", false );
}

template <class Iterable>
iloc_stack_t get_stacks( Iterable items, filoc_t const &iloc_helper )
{
    iloc_stack_t stacks;
    stack_cache_t cache;
    for( item &elem : items ) {
        _get_stacks( &elem, &stacks, &cache, iloc_helper );
    }

    return stacks;
}

// all_items_top() returns an Iterable of element pointers unlike map::i_at() and friends (which
// return an Iterable of elements) so we need this specialization and minor code duplication.
iloc_stack_t get_stacks( std::list<item *> const &items, filoc_t const &iloc_helper )
{
    iloc_stack_t stacks;
    stack_cache_t cache;
    for( item *elem : items ) {
        _get_stacks( elem, &stacks, &cache, iloc_helper );
    }

    return stacks;
}

aim_advuilist_t::count_t iloc_entry_counter( iloc_entry const &it )
{
    return it.stack[0]->count_by_charges() ? it.stack[0]->charges : it.stack.size();
}

std::string iloc_entry_count( iloc_entry const &it, int width )
{
    return string_format( "%*d", width, iloc_entry_counter( it ) );
}

std::string iloc_entry_weight( iloc_entry const &it, int width )
{
    return _format_value( convert_weight( _iloc_entry_weight( it ) ), width );
}

std::string iloc_entry_volume( iloc_entry const &it, int width )
{
    return _format_value( convert_volume( _iloc_entry_volume( it ).value() ), width );
}

std::string iloc_entry_name( iloc_entry const &it, int /* width */ )
{
    item const &i = *it.stack[0];
    std::string name = i.count_by_charges() ? i.tname() : i.display_name();
    if( !i.is_owned_by( get_avatar(), true ) ) {
        name = string_format( "%s %s", colorize( "!", c_light_red ), name );
    }
    nc_color const basecolor = i.color_in_inventory();
    nc_color const color =
        get_auto_pickup().has_rule( &i ) ? magenta_background( basecolor ) : basecolor;
    return colorize( name, color );
}

bool iloc_entry_count_sorter( iloc_entry const &l, iloc_entry const &r )
{
    return iloc_entry_counter( l ) > iloc_entry_counter( r );
}

bool iloc_entry_weight_sorter( iloc_entry const &l, iloc_entry const &r )
{
    return _iloc_entry_weight( l ) > _iloc_entry_weight( r );
}

bool iloc_entry_volume_sorter( iloc_entry const &l, iloc_entry const &r )
{
    return _iloc_entry_volume( l ) > _iloc_entry_volume( r );
}

bool iloc_entry_damage_sorter( iloc_entry const &l, iloc_entry const &r )
{
    return l.stack.front()->damage() > r.stack.front()->damage();
}

bool iloc_entry_spoilage_sorter( iloc_entry const &l, iloc_entry const &r )
{
    return l.stack.front()->spoilage_sort_order() < r.stack.front()->spoilage_sort_order();
}

bool iloc_entry_price_sorter( iloc_entry const &l, iloc_entry const &r )
{
    Character const &u = get_avatar();
    int const lprice =
        npc_trading::trading_price( u, u, { l.stack.front(), iloc_entry_counter( l ) } );
    int const rprice =
        npc_trading::trading_price( u, u, { r.stack.front(), iloc_entry_counter( r ) } );
    return lprice > rprice;
}

bool iloc_entry_name_sorter( iloc_entry const &l, iloc_entry const &r )
{
    std::string const &_ln = l.stack[0]->tname( 1, false );
    std::string const &_rn = r.stack[0]->tname( 1, false );
    if( _ln == _rn ) {
        return localized_compare( l.stack[0]->tname(), r.stack[0]->tname() );
    }

    return localized_compare( _ln, _rn );
}

bool iloc_entry_gsort( iloc_entry const &l, iloc_entry const &r )
{
    return l.stack[0]->get_category_of_contents() < r.stack[0]->get_category_of_contents();
}

std::string iloc_entry_glabel( iloc_entry const &it )
{
    return it.stack[0]->get_category_of_contents().name();
}

bool iloc_entry_filter( iloc_entry const &it, std::string const &filter )
{
    auto const filterf = item_filter_from_string( filter );
    return filterf( *it.stack[0] );
}

void iloc_entry_stats( aim_stats_t *stats, bool reset, iloc_entry const &it )
{
    if( reset ) {
        stats->mass = 0_kilogram;
        stats->volume = 0_liter;
    } else {
        for( auto const &v : it.stack ) {
            stats->mass += v->weight();
            stats->volume += v->volume();
        }
    }
}

void iloc_entry_examine( catacurses::window *w, iloc_entry const &it )
{
    item const &_it = *it.stack.front();
    std::vector<iteminfo> vThisItem;
    std::vector<iteminfo> vDummy;
    _it.info( true, vThisItem );

    item_info_data data( _it.tname(), _it.type_name(), vThisItem, vDummy );
    data.handle_scrolling = true;

    draw_item_info( *w, data ).get_first_input();
}

aim_container_t source_ground( tripoint const &loc )
{
    return get_stacks<>( get_map().i_at( loc ),
    [&]( item * it ) {
        return item_location( map_cursor( loc ), it );
    } );
}

aim_container_t source_vehicle( tripoint const &loc )
{
    cata::optional<vpart_reference> vp = veh_cargo_at( loc );

    int const part_index = static_cast<int>( vp->part_index() );

    return get_stacks<>( vp->vehicle().get_items( part_index ), [&]( item * it ) {
        return item_location( vehicle_cursor( vp->vehicle(), part_index ), it );
    } );
}

bool source_vehicle_avail( tripoint const &loc )
{
    cata::optional<vpart_reference> vp = veh_cargo_at( loc );
    return vp.has_value() && !vp->part().is_cleaner_on();
}

aim_container_t source_char_inv( Character *guy )
{
    aim_container_t ret;
    for( item_location &worn_item : guy->top_items_loc() ) {
        _append_stacks( guy, worn_item, &ret );
    }

    item_location weapon = guy->get_wielded_item();
    if( weapon and weapon->is_container() ) {
        _append_stacks( guy, weapon, &ret );
    }

    return ret;
}

aim_container_t source_char_worn( Character *guy )
{
    aim_container_t ret;
    for( item_location const &worn_item : guy->top_items_loc() ) {
        ret.emplace_back( iloc_entry{ { worn_item } } );
    }

    item_location weapon = guy->get_wielded_item();
    if( weapon ) {
        ret.emplace_back( iloc_entry{ { weapon } } );
    }

    return ret;
}

template iloc_stack_t get_stacks<>( map_stack items, filoc_t const &iloc_helper );
template iloc_stack_t get_stacks<>( vehicle_stack items, filoc_t const &iloc_helper );

} // namespace advuilist_helpers
