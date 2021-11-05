#include "advanced_inv_ng.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "activity_actor_definitions.h"
#include "advuilist.h"
#include "advuilist_const.h"
#include "advuilist_helpers.h"
#include "advuilist_sourced.h"
#include "auto_pickup.h"
#include "avatar.h"
#include "cata_assert.h"
#include "color.h"
#include "enums.h"
#include "game.h"
#include "input.h"
#include "item.h"
#include "item_location.h"
#include "json.h"
#include "line.h"
#include "map.h"
#include "optional.h"
#include "options.h"
#include "output.h"
#include "panels.h"
#include "player_activity.h"
#include "point.h"
#include "string_formatter.h"
#include "transaction_ui.h"
#include "translations.h"
#include "type_id.h"
#include "ui.h"
#include "ui_manager.h"
#include "uistate.h"
#include "units.h"
#include "units_utility.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vpart_position.h"

namespace catacurses
{
class window;
} // namespace catacurses

static activity_id const ACT_ADV_INVENTORY( "ACT_ADV_INVENTORY" );
static activity_id const ACT_WEAR( "ACT_WEAR" );

namespace
{
transaction_ui_save_state adv_inv_default_state, adv_inv_state;
constexpr char const *adv_inv_state_str = "adv_inv_state";
constexpr char const *adv_inv_default_state_str = "adv_inv_default_state";

using namespace advuilist_helpers;

constexpr std::size_t aim_nsources = 18;
constexpr point aimlayout{ 6, 3 };

constexpr char SOURCE_DRAGGED_i = 'D';
constexpr char SOURCE_VEHICLE_i = 'V';
constexpr char const *ACTION_EXAMINE = "EXAMINE";
constexpr char const *ACTION_ITEMS_DEFAULT = "ITEMS_DEFAULT";
constexpr char const *ACTION_SAVE_DEFAULT = "SAVE_DEFAULT";
constexpr char const *TOGGLE_AUTO_PICKUP = "TOGGLE_AUTO_PICKUP";
constexpr char const *TOGGLE_FAVORITE = "TOGGLE_FAVORITE";


struct _aim_source_t {
    aim_advuilist_sourced_t::icon_t icon = 0;
    direction dir;
};
using _sourcearray = std::array<_aim_source_t, aim_nsources>;
constexpr _sourcearray aimsources = {
    _aim_source_t{ 'C', direction::last },
    _aim_source_t{ SOURCE_DRAGGED_i, direction::last },
    _aim_source_t{ 0, direction::last },
    _aim_source_t{ '7', direction::NORTHWEST },
    _aim_source_t{ '8', direction::NORTH },
    _aim_source_t{ '9', direction::NORTHEAST },
    _aim_source_t{ 0, direction::last },
    _aim_source_t{ 'I', direction::last },
    _aim_source_t{ 0, direction::last },
    _aim_source_t{ '4', direction::WEST },
    _aim_source_t{ '5', direction::CENTER },
    _aim_source_t{ '6', direction::EAST },
    _aim_source_t{ 'A', direction::last },
    _aim_source_t{ 'W', direction::last },
    _aim_source_t{ 0, direction::last },
    _aim_source_t{ '1', direction::SOUTHWEST },
    _aim_source_t{ '2', direction::SOUTH },
    _aim_source_t{ '3', direction::SOUTHEAST },
};

constexpr _sourcearray::size_type CONT_IDX = 0;
constexpr _sourcearray::size_type DRAGGED_IDX = 1;
constexpr _sourcearray::size_type INV_IDX = 7;
constexpr _sourcearray::size_type ALL_IDX = 12;
constexpr _sourcearray::size_type WORN_IDX = 13;

constexpr bool is_ground_source( _aim_source_t const &s )
{
    return s.dir != direction::last;
}

tripoint source_to_offset( _aim_source_t const &s )
{
    return is_ground_source( s ) ? displace( s.dir ) : tripoint_zero;
}

tripoint slotidx_to_offset( aim_advuilist_sourced_t::slotidx_t idx )
{
    if( idx == DRAGGED_IDX ) {
        return get_avatar().grab_point;
    }

    return source_to_offset( aimsources[idx] );
}

_sourcearray::size_type offset_to_slotidx( tripoint const &off )
{
    _sourcearray::const_iterator const it =
    std::find_if( aimsources.begin(), aimsources.end(), [&]( _aim_source_t const & v ) {
        return is_ground_source( v ) and displace( v.dir ) == off;
    } );
    return std::distance( aimsources.begin(), it );
}

constexpr bool is_vehicle( aim_advuilist_sourced_t::icon_t icon )
{
    return icon == SOURCE_DRAGGED_i or icon == SOURCE_VEHICLE_i;
}

bool is_dragged( aim_advuilist_sourced_t::getsource_t const &src )
{
    return is_vehicle( src.icon ) and
           ( src.slotidx == DRAGGED_IDX or
             src.slotidx == offset_to_slotidx( get_avatar().grab_point ) );
}

std::string sourcelabel( _sourcearray::size_type idx )
{
    switch( idx ) {
        case CONT_IDX:
            return _( "Container" );
        case DRAGGED_IDX:
            return _( "Grabbed Vehicle" );
        case INV_IDX:
            return _( "Inventory" );
        case ALL_IDX:
            return _( "Surrounding Area" );
        case WORN_IDX:
            return _( "Worn Items" );
        default: {
            if( is_ground_source( aimsources[idx] ) ) {
                return direction_name( aimsources[idx].dir );
            }
        }
    }

    return {};
}

bool source_player_dragged_avail()
{
    avatar const &u = get_avatar();
    if( u.get_grab_type() == object_type::VEHICLE ) {
        return source_vehicle_avail( u.pos() + u.grab_point );
    }

    return false;
}

aim_container_t source_player_ground( tripoint const &offset )
{
    return source_ground( get_avatar().pos() + offset );
}

bool source_player_ground_avail( tripoint const &offset )
{
    return get_map().can_put_items_ter_furn( get_avatar().pos() + offset );
}

aim_container_t source_player_vehicle( tripoint const &offset )
{
    return source_vehicle( get_avatar().pos() + offset );
}

bool source_player_vehicle_avail( tripoint const &offset )
{
    return source_vehicle_avail( get_avatar().pos() + offset );
}

aim_container_t source_player_dragged()
{
    avatar &u = get_avatar();
    return source_vehicle( u.pos() + u.grab_point );
}

aim_container_t source_player_inv()
{
    return source_char_inv( &get_avatar() );
}

aim_container_t source_player_worn()
{
    return source_char_worn( &get_avatar() );
}

aim_container_t source_player_all( aim_transaction_ui_t *ui )
{
    aim_container_t itemlist;
    _sourcearray::size_type idx = 0;
    using getsource_t = aim_advuilist_sourced_t::getsource_t;
    aim_advuilist_sourced_t *otherpane = ui->left()->get_source().slotidx == ALL_IDX ? ui->right() :
                                         ui->left();
    getsource_t osrc = otherpane->get_source();
    if( osrc.slotidx == DRAGGED_IDX ) {
        osrc.slotidx = offset_to_slotidx( get_avatar().grab_point );
    }

    for( auto const &v : aimsources ) {
        if( is_ground_source( v ) ) {
            tripoint const off = source_to_offset( v );
            if( ( idx != osrc.slotidx or is_vehicle( osrc.icon ) ) and
                source_player_ground_avail( off ) ) {
                aim_container_t const &stacks = source_player_ground( off );
                itemlist.insert( itemlist.end(), std::make_move_iterator( stacks.begin() ),
                                 std::make_move_iterator( stacks.end() ) );
            }
            if( ( idx != osrc.slotidx or !is_vehicle( osrc.icon ) ) and
                source_player_vehicle_avail( off ) ) {
                aim_container_t const &stacks = source_player_vehicle( off );
                itemlist.insert( itemlist.end(), std::make_move_iterator( stacks.begin() ),
                                 std::make_move_iterator( stacks.end() ) );
            }
        }
        idx++;
    }

    return itemlist;
}

std::string iloc_entry_src( iloc_entry const &it, int /* width */ )
{
    tripoint const off = it.stack.front().position() - get_avatar().pos();
    return trim( direction_name_short( aimsources[offset_to_slotidx( off )].dir ) );
}

std::pair<point, point> aim_size( bool full_screen )
{
    int const min_w_width = FULL_SCREEN_WIDTH;
    int const max_w_width = full_screen ? TERMX : std::max( 120,
                            TERMX - 2 * ( panel_manager::get_manager().get_width_right() +
                                          panel_manager::get_manager().get_width_left() ) );

    int const width = TERMX < min_w_width
                      ? min_w_width
                      : TERMX > max_w_width ? max_w_width : TERMX;
    int const originx = TERMX > width ? ( TERMX - width ) / 2 : 0;

    return { point{width, TERMY}, point{originx, 0} };
}

std::string aim_sourcelabel( _sourcearray::size_type idx, bool veh = false )
{
    _aim_source_t const &src = aimsources[idx];
    tripoint const pos = get_avatar().pos() + slotidx_to_offset( idx );
    std::string prefix = sourcelabel( idx );
    std::string label;

    if( ( veh and source_vehicle_avail( pos ) ) or
        ( idx == DRAGGED_IDX and source_player_dragged_avail() ) ) {
        cata::optional<vpart_reference> vp = veh_cargo_at( pos );
        prefix = vp->vehicle().name;
        label = vp->get_label().value_or( vp->info().name() );
    } else {
        if( is_ground_source( src ) ) {
            label = get_map().name( pos );
        }
    }

    return string_format( "%s\n%s", colorize( prefix, c_green ), colorize( label, c_light_blue ) );
}

void aim_inv_idv_stats( aim_advuilist_sourced_t *ui )
{
    aim_transaction_ui_t::select_t const peek = ui->peek();
    avatar const &u = get_avatar();

    if( !peek.empty() ) {
        iloc_entry const &entry = *peek.front().ptr;
        double const peek_len = convert_length_cm_in( entry.stack[0]->length() );
        double const indiv_len_cap = convert_length_cm_in( u.max_single_item_length() );
        std::string const peek_len_str = colorize(
                                             string_format( "%.1f", peek_len ), peek_len > indiv_len_cap ? c_red : c_light_green );
        std::string const indiv_len_cap_str = string_format( "%.1f", indiv_len_cap );
        bool const metric = get_option<std::string>( "DISTANCE_UNITS" ) == "metric";
        std::string const len_unit = metric ? "cm" : "in";

        units::volume const indiv_vol_cap = u.max_single_item_volume();
        units::volume const peek_vol = entry.stack[0]->volume();
        std::string const indiv_vol_cap_str = format_volume( indiv_vol_cap );
        std::string const peek_vol_str =
            colorize( format_volume( peek_vol ), peek_vol > indiv_vol_cap ? c_red : c_light_green );

        right_print( *ui->get_window(), 2, 2, c_white,
                     string_format( _( "INDV %s/%s %s  %s/%s %s" ), peek_len_str, indiv_len_cap_str,
                                    len_unit, peek_vol_str, indiv_vol_cap_str,
                                    volume_units_abbr() ) );
    }
}

void aim_inv_stats( aim_advuilist_sourced_t *ui )
{
    avatar const &u = get_avatar();
    double const weight = convert_weight( u.weight_carried() );
    double const weight_cap = convert_weight( u.weight_capacity() );
    std::string const weight_str =
        colorize( string_format( "%.1f", weight ), weight >= weight_cap ? c_red : c_light_green );

    right_print( *ui->get_window(), 1, 2, c_white,
                 string_format( "%s/%.1f %s  %s/%s %s", weight_str, weight_cap, weight_units(),
                                format_volume( u.volume_carried() ),
                                format_volume( u.volume_capacity() ), volume_units_abbr() ) );
}

void aim_ground_veh_stats( aim_advuilist_sourced_t *ui, aim_stats_t *stats )
{
    aim_advuilist_sourced_t::getsource_t const src = ui->get_source();
    tripoint const loc = get_avatar().pos() + slotidx_to_offset( src.slotidx );
    units::volume vol_cap = 0_liter;

    if( is_vehicle( src.icon ) ) {
        cata::optional<vpart_reference> const vp = veh_cargo_at( loc );
        vol_cap = vp ? vp->vehicle().max_volume( static_cast<int>( vp->part_index() ) ) : 0_liter;
    } else {
        vol_cap = get_map().max_volume( loc );
    }

    right_print( *ui->get_window(), 1, 2, c_white,
                 string_format( "%3.1f %s  %s/%s %s", convert_weight( stats->mass ), weight_units(),
                                format_volume( stats->volume ), format_volume( vol_cap ),
                                volume_units_abbr() ) );
}

void aim_default_columns( aim_advuilist_t *myadvuilist )
{
    using col_t = typename aim_advuilist_t::col_t;
    myadvuilist->set_columns( std::vector<col_t> { col_t{ "Name", iloc_entry_name, 16 },
                              col_t{ "amt", iloc_entry_count, 2 },
                              col_t{ "weight", iloc_entry_weight, 3 },
                              col_t{ "vol", iloc_entry_volume, 3 }
                                                 },
                              false );
}

void aim_all_columns( aim_advuilist_t *myadvuilist )
{
    using col_t = typename aim_advuilist_t::col_t;
    myadvuilist->set_columns( std::vector<col_t> { col_t{ "Name", iloc_entry_name, 16 },
                              col_t{ "src", iloc_entry_src, 2 },
                              col_t{ "amt", iloc_entry_count, 2 },
                              col_t{ "weight", iloc_entry_weight, 3 },
                              col_t{ "vol", iloc_entry_volume, 3 }
                                                 },
                              false );
}

void aim_stats_printer( aim_advuilist_sourced_t *_ui, aim_stats_t *stats )
{
    using slotidx_t = aim_advuilist_sourced_t::slotidx_t;
    slotidx_t const src = _ui->get_source().slotidx;

    if( src == INV_IDX or src == WORN_IDX ) {
        aim_inv_stats( _ui );
    } else {
        aim_ground_veh_stats( _ui, stats );
        aim_inv_idv_stats( _ui );
    }
}

void player_take_off( aim_transaction_ui_t::select_t const &sel )
{
    for( auto const &it : sel ) {
        cata_assert( it.ptr->stack.size() == 1 );
        get_avatar().takeoff( it.ptr->stack[0] );
    }
}

std::vector<drop_or_stash_item_info>
_get_selection_amount( aim_transaction_ui_t::select_t const &sel, bool ignorefav = false )
{
    bool const _ifav = sel.size() > 1 and ignorefav;
    std::vector<drop_or_stash_item_info> selection;
    std::vector<item_location> corpses;
    for( auto const &it : sel ) {
        if( _ifav and it.ptr->stack.front()->is_favorite ) {
            continue;
        }

        bool const is_corpse = it.ptr->stack.front()->is_corpse();
        if( it.ptr->stack.front()->count_by_charges() ) {
            selection.emplace_back( it.ptr->stack.front(), it.count );
            if( is_corpse ) {
                corpses.emplace_back( it.ptr->stack.front() );
            }
        } else {
            using diff_type = decltype( it.ptr->stack )::difference_type;
            cata_assert( it.count <= it.ptr->stack.size() );
            for( auto v = it.ptr->stack.begin();
                 v != it.ptr->stack.begin() + static_cast<diff_type>( it.count ); ++v ) {
                selection.emplace_back( *v, it.count );
                if( is_corpse ) {
                    corpses.emplace_back( *v );
                }
            }
        }
    }

    // FIXME: super ugly and astyle insists on making it worse
    // Remove selected items if their corpse parent was also selected. similar
    // to inventory_multiselector::deselect_contained_items()
    auto const new_end = std::remove_if(
    selection.begin(), selection.end(), [&corpses]( drop_or_stash_item_info const & elem ) {
        return std::count_if(
        corpses.begin(), corpses.end(), [&elem]( item_location const & corpse ) {
            return elem.loc().has_parent() && elem.loc().parent_item() == corpse;
        } ) > 0;
    } );
    selection.erase( new_end, selection.end() );
    return selection;
}

void player_drop( aim_transaction_ui_t::select_t const &sel, tripoint const pos, bool to_vehicle )
{
    std::vector<drop_or_stash_item_info> const to_drop = _get_selection_amount( sel, true );
    get_avatar().assign_activity(
        player_activity( drop_activity_actor( to_drop, pos, !to_vehicle ) ) );
}

void get_selection_amount( aim_transaction_ui_t::select_t const &sel,
                           std::vector<item_location> *targets, std::vector<int> *quantities,
                           bool ignorefav = false )
{

    std::vector<drop_or_stash_item_info> const sele = _get_selection_amount( sel, ignorefav );
    for( drop_or_stash_item_info const &it : sele ) {
        targets->emplace_back( it.loc() );
        quantities->emplace_back( it.count() );
    }
}

void player_wear( aim_transaction_ui_t::select_t const &sel )
{
    avatar &u = get_avatar();

    std::vector<item_location> targets;
    std::vector<int> quantities;
    get_selection_amount( sel, &targets, &quantities );

    u.assign_activity( player_activity( wear_activity_actor( targets, quantities ) ) );
}

void player_pick_up( aim_transaction_ui_t::select_t const &sel, bool from_vehicle )
{
    avatar &u = get_avatar();

    std::vector<item_location> targets;
    std::vector<int> quantities;
    get_selection_amount( sel, &targets, &quantities );

    u.assign_activity( player_activity( pickup_activity_actor(
                                            targets, quantities, from_vehicle ? cata::nullopt : cata::optional<tripoint>( u.pos() ),
                                            false ) ) );
}

void player_move_items( aim_transaction_ui_t::select_t const &sel, tripoint const pos,
                        bool to_vehicle )
{
    std::vector<item_location> targets;
    std::vector<int> quantities;
    get_selection_amount( sel, &targets, &quantities, true );

    get_avatar().assign_activity(
        player_activity( move_items_activity_actor( targets, quantities, to_vehicle, pos ) ) );
}

void change_columns( aim_advuilist_sourced_t *ui )
{
    if( ui->get_source().slotidx == ALL_IDX ) {
        aim_all_columns( ui ) ;
    } else {
        aim_default_columns( ui );
    }
}

int query_destination()
{
    uilist menu;
    menu.text = _( "Select destination" );
    int idx = 0;
    for( _aim_source_t const &v : aimsources ) {
        tripoint const off = source_to_offset( v );
        if( idx != ALL_IDX and is_ground_source( v ) ) {
            bool const valid = source_player_ground_avail( off );
            menu.addentry( idx, valid, MENU_AUTOASSIGN, sourcelabel( idx ) );
        }
        idx++;
    }
    menu.query();
    return menu.ret;
}

bool swap_panes_maybe( aim_transaction_ui_t *ui, bool mouse )
{
    using namespace advuilist_literals;
    using getsource_t = aim_advuilist_sourced_t::getsource_t;
    getsource_t const psrc = ui->cur_pane()->get_source_prev();
    getsource_t const csrc = ui->cur_pane()->get_source();
    getsource_t const osrc = ui->other_pane()->get_source();
    // swap panes if the requested source is already selected in the other pane
    // also swap panes if the current source is re-selected since people have grown accustomed
    // to this behaviour (see discussion in #45900)
    if( csrc.avail and osrc.avail and
        ( csrc == osrc or ( csrc == psrc and !mouse ) or ( is_dragged( csrc ) and is_dragged( osrc ) ) ) ) {
        // undo set_source()
        ui->cur_pane()->set_source( psrc.slotidx, psrc.icon, false, false );

        ui->push_event( aim_transaction_ui_t::event::SWAP );
        ui->cur_pane()->suspend();

        return true;
    }

    return false;
}

void aim_rebuild( aim_transaction_ui_t *ui )
{
    ui->left()->rebuild();
    ui->right()->rebuild();
}

void setup_for_aim( aim_advuilist_sourced_t *myadvuilist, aim_stats_t *stats )
{
    using sorter_t = typename aim_advuilist_t::sorter_t;
    using grouper_t = typename aim_advuilist_t::grouper_t;
    using filter_t = typename aim_advuilist_t::filter_t;

    aim_default_columns( myadvuilist );
    myadvuilist->set_fcounting( iloc_entry_counter );
    // we need to replace name sorter due to color tags
    myadvuilist->add_sorter( sorter_t{ "Name", iloc_entry_name_sorter } );
    // use numeric sorters instead of advuilist's lexicographic ones
    myadvuilist->add_sorter( sorter_t{ "amount", iloc_entry_count_sorter } );
    myadvuilist->add_sorter( sorter_t{ "weight", iloc_entry_weight_sorter } );
    myadvuilist->add_sorter( sorter_t{ "volume", iloc_entry_volume_sorter } );
    // extra sorters
    myadvuilist->add_sorter( sorter_t{ "damage", iloc_entry_damage_sorter} );
    myadvuilist->add_sorter( sorter_t{ "spoilage", iloc_entry_spoilage_sorter} );
    myadvuilist->add_sorter( sorter_t{ "price", iloc_entry_price_sorter} );
    myadvuilist->add_grouper( grouper_t{ "category", iloc_entry_gsort, iloc_entry_glabel } );
    myadvuilist->set_ffilter( filter_t{ std::string(), iloc_entry_filter } );
    myadvuilist->on_rebuild(
    [stats]( bool first, iloc_entry const & it ) {
        iloc_entry_stats( stats, first, it );
    } );
    myadvuilist->on_redraw(
    [stats]( aim_advuilist_sourced_t *ui ) {
        aim_stats_printer( ui, stats );
    } );
    myadvuilist->get_ctxt()->register_action( ACTION_EXAMINE );
    myadvuilist->get_ctxt()->register_action( ACTION_ITEMS_DEFAULT );
    myadvuilist->get_ctxt()->register_action( ACTION_SAVE_DEFAULT );
    myadvuilist->get_ctxt()->register_action( TOGGLE_AUTO_PICKUP );
    myadvuilist->get_ctxt()->register_action( TOGGLE_FAVORITE );
}

void add_aim_sources( aim_advuilist_sourced_t *myadvuilist, aim_transaction_ui_t *mytrui )
{
    using source_t = aim_advuilist_sourced_t::source_t;
    using fsource_t = aim_advuilist_sourced_t::fsource_t;
    using fsourceb_t = aim_advuilist_sourced_t::fsourceb_t;
    using flabel_t = aim_advuilist_sourced_t::flabel_t;

    fsource_t source_dummy = []() {
        return aim_container_t();
    };
    fsourceb_t _never = []() {
        return false;
    };
    fsourceb_t _always = []() {
        return true;
    };

    _sourcearray::size_type idx = 0;
    for( auto const &src : aimsources ) {
        fsource_t _fs;
        fsource_t _fsv;
        fsourceb_t _fsb;
        fsourceb_t _fsvb;

        if( src.icon != 0 ) {
            switch( idx ) {
                case CONT_IDX: {
                    _fs = source_dummy;
                    _fsb = _never;
                    break;
                }
                case DRAGGED_IDX: {
                    _fs = source_player_dragged;
                    _fsb = source_player_dragged_avail;
                    break;
                }
                case INV_IDX: {
                    _fs = source_player_inv;
                    _fsb = _always;
                    break;
                }
                case ALL_IDX: {
                    _fs = [ = ]() {
                        return source_player_all( mytrui );
                    };
                    _fsb = _always;
                    break;
                }
                case WORN_IDX: {
                    _fs = source_player_worn;
                    _fsb = _always;
                    break;
                }
                default: {
                    tripoint const offset = source_to_offset( src );
                    _fs = [ = ]() {
                        return source_player_ground( offset );
                    };
                    _fsb = [ = ]() {
                        return source_player_ground_avail( offset );
                    };
                    _fsv = [ = ]() {
                        return source_player_vehicle( offset );
                    };
                    _fsvb = [ = ]() {
                        return source_player_vehicle_avail( offset );
                    };
                    break;
                }
            }
            flabel_t const label = [ = ]() {
                return aim_sourcelabel( idx );
            };
            myadvuilist->add_source( idx, source_t{ label, src.icon, _fs, _fsb } );
            if( _fsv ) {
                flabel_t const vlabel = [ = ]() {
                    return aim_sourcelabel( idx, true );
                };
                myadvuilist->add_source( idx, source_t{ vlabel, SOURCE_VEHICLE_i, _fsv, _fsvb } );
            }
        }
        idx++;
    }
}

void aim_add_return_activity()
{
    player_activity act_return( ACT_ADV_INVENTORY );
    act_return.auto_resume = true;
    get_avatar().assign_activity( act_return );
}

void aim_transfer( aim_transaction_ui_t *ui, aim_transaction_ui_t::select_t const &select )
{
    using slotidx_t = aim_advuilist_sourced_t::slotidx_t;
    using getsource_t = aim_advuilist_sourced_t::getsource_t;
    getsource_t const csrc = ui->cur_pane()->get_source();
    getsource_t dst = ui->other_pane()->get_source();

    if( !dst.avail ) {
        popup( _( "You can't put items there!" ) );
        return;
    }

    // select a valid destination if otherpane is showing the ALL source
    if( dst.slotidx == ALL_IDX ) {
        int const newdst = query_destination();
        if( newdst < 0 ) {
            // transfer cancelled
            return;
        }
        dst.slotidx = static_cast<slotidx_t>( newdst );
        dst.icon = aimsources[dst.slotidx].icon;
    }

    // return to the AIM after player activities finish
    if( select.size() == 1 or !get_option<bool>( "CLOSE_ADV_INV" ) ) {
        aim_add_return_activity();
    }

    if( dst.slotidx == WORN_IDX ) {
        player_wear( select );
    } else if( csrc.slotidx == WORN_IDX and dst.slotidx == INV_IDX ) {
        player_take_off( select );
    } else if( csrc.slotidx == WORN_IDX or csrc.slotidx == INV_IDX ) {
        player_drop( select, slotidx_to_offset( dst.slotidx ), is_vehicle( dst.icon ) );
    } else if( dst.slotidx == INV_IDX ) {
        player_pick_up( select, is_vehicle( csrc.icon ) );
    } else {
        player_move_items( select, slotidx_to_offset( dst.slotidx ), is_vehicle( dst.icon ) );
    }

    ui->push_event( aim_transaction_ui_t::event::ACTIVITY );
}

void aim_examine( aim_transaction_ui_t *ui, iloc_entry &entry )
{
    aim_advuilist_sourced_t::slotidx_t const src = ui->cur_pane()->get_source().slotidx;
    if( src == INV_IDX or src == WORN_IDX ) {
        aim_add_return_activity();
        ui->push_event( aim_transaction_ui_t::event::QUIT );
        ui->cur_pane()->suspend();
        ui->cur_pane()->hide();
        ui->other_pane()->hide();

        std::pair<point, point> const dim = ui->other_pane()->get_size();
        game::inventory_item_menu_position const side =
            ui->cur_pane() == ui->left() ? game::LEFT_OF_INFO : game::RIGHT_OF_INFO;
        g->inventory_item_menu(
            entry.stack.front(), [ = ] { return dim.second.x; }, [ = ] { return dim.first.x; }, side );
    } else {
        iloc_entry_examine( ui->other_pane()->get_window(), entry );
    }
}

void aim_ctxthandler( aim_transaction_ui_t *ui, std::string const &action )
{
    using namespace advuilist_literals;
    using select_t = aim_transaction_ui_t::select_t;
    select_t const peek = ui->cur_pane()->peek();

    if( action == ACTION_CYCLE_SOURCES or
        action.substr( 0, ACTION_SOURCE_PRFX_len ) == ACTION_SOURCE_PRFX or
        action == ACTION_MOUSE_SELECT ) {

        bool const swapped = swap_panes_maybe( ui, action == ACTION_MOUSE_SELECT );

        change_columns( ui->cur_pane() );
        // rebuild other pane if it's set to the ALL source
        if( !swapped and ui->other_pane()->get_source().slotidx == ALL_IDX ) {
            ui->other_pane()->rebuild();
            ui->other_pane()->get_ui()->invalidate_ui();
        }

    } else if( action == ACTION_SAVE_DEFAULT ) {
        ui->save_state( &adv_inv_default_state );

    } else if( action == ACTION_ITEMS_DEFAULT ) {
        ui->cur_pane()->suspend();
        ui->load_state( adv_inv_default_state, false );
        aim_rebuild( ui );
        ui->other_pane()->get_ui()->invalidate_ui();

    } else if( action == ACTION_FILTER ) {
        ui->other_pane()->get_ui()->invalidate_ui();

    } else if( !peek.empty() ) {
        iloc_entry &entry = *peek.front().ptr;

        if( action == ACTION_EXAMINE ) {
            aim_examine( ui, entry );

        } else if( action == TOGGLE_AUTO_PICKUP ) {
            item const *it = entry.stack.front().get_item();
            bool const has = get_auto_pickup().has_rule( it );
            if( !has ) {
                get_auto_pickup().add_rule( it, true );
            } else {
                get_auto_pickup().remove_rule( it );
            }

        } else if( action == TOGGLE_FAVORITE ) {
            bool const isfavorite = entry.stack.front()->is_favorite;
            for( item_location &it : entry.stack ) {
                it->set_favorite( !isfavorite );
            }
        }
    }
}

// sane default AIM state: left pane shows All source, right pane shows Inventory.
// Both are sorted by name and grouped by category, with no filter
// FIXME: magic numbers bad. maybe stringify sorter and grouper in advuilist_save_state
transaction_ui_save_state const aim_default_state{
    advuilist_save_state{ ALL_IDX, 0, 1, 1, 0, std::string(), false },
    advuilist_save_state{ INV_IDX, 0, 1, 1, 0, std::string(), false }, 0 };

using aim_t = transaction_ui<aim_container_t>;
std::unique_ptr<aim_t> aim_ui;
} // namespace

void save_adv_inv_state( JsonOut &json )
{
    json.member( adv_inv_state_str, adv_inv_state );
    json.member( adv_inv_default_state_str, adv_inv_default_state );
}

void load_adv_inv_state( const JsonObject &jo )
{
    jo.read( adv_inv_state_str, adv_inv_state );
    jo.read( adv_inv_default_state_str, adv_inv_default_state );
}

void create_advanced_inv( bool resume )
{
    static aim_stats_t lstats{ 0_kilogram, 0_liter };
    static aim_stats_t rstats{ 0_kilogram, 0_liter };
    if( !aim_ui ) {
        aim_ui = std::make_unique<aim_t>( aimlayout, point_zero, point_zero,
                                          "ADVANCED_INVENTORY", point{ 3, 1 } );
        aim_ui->on_resize( []( aim_t * ui ) {
            bool const full_screen{ get_option<bool>( "AIM_WIDTH" ) };
            std::pair<point, point> const size = aim_size( full_screen );
            ui->resize( size.first, size.second );
        } );

        setup_for_aim( aim_ui->left(), &lstats );
        setup_for_aim( aim_ui->right(), &rstats );
        add_aim_sources( aim_ui->left(), aim_ui.get() );
        add_aim_sources( aim_ui->right(), aim_ui.get() );
        auto const filterdesc = [&]( aim_advuilist_t *ui ) {
            point const size = ui->get_size().first;
            draw_item_filter_rules( *aim_ui->other_pane()->get_window(), 1, size.y - 2,
                                    item_filter_type::FILTER );
        };
        aim_ui->left()->on_filter( filterdesc );
        aim_ui->right()->on_filter( filterdesc );
        aim_ui->on_select( aim_transfer );
        aim_ui->on_input( [&]( aim_transaction_ui_t *ui, std::string const & action ) {
            aim_ctxthandler( ui, action );
        } );
    }

    transaction_ui_save_state const &saved_state =
        !resume and get_option<bool>( "OPEN_DEFAULT_ADV_INV" )
        ? adv_inv_default_state
        : adv_inv_state;

    if( saved_state.initialized ) {
        aim_ui->load_state( saved_state, false );
    } else {
        aim_ui->load_state( aim_default_state, false );
    }

    aim_rebuild( &*aim_ui );

    aim_ui->show();
    aim_ui->save_state( &adv_inv_state );
}

void kill_advanced_inv()
{
    if( aim_ui ) {
        aim_ui->hide();
    }
}
