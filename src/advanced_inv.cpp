#include "advanced_inv.h"

#include <algorithm> // for max
#include <array>     // for array
#include <memory>    // for unique_ptr
#include <string>    // for operator==, basic_string, string
#include <utility>   // for move
#include <vector>    // for vector

#include "activity_actor_definitions.h"  // for drop_or_stash_item_info, dro...
#include "advuilist.h"                   // for advuilist<>::groupcont_t
#include "advuilist_helpers.h"           // for aim_advuilist_sourced_t, aim...
#include "advuilist_sourced.h"           // for advuilist_sourced
#include "auto_pickup.h"                 // for get_auto_pickup, player_sett...
#include "avatar.h"                      // for get_avatar, avatar
#include "game.h"                        // for game, g, game::LEFT_OF_INFO
#include "map.h"                         // for get_map, map
#include "options.h"                     // for get_option
#include "output.h"                      // for format_volume, right_print
#include "panels.h"                      // for panel_manager
#include "point.h"                       // for tripoint, point, tripoint_zero
#include "activity_type.h"               // for activity_id
#include "advuilist_const.h"             // for ACTION_CYCLE_SOURCES, ACTION...
#include "color.h"                       // for colorize, color_manager, c_l...
#include "enums.h"                       // for object_type, object_type::VE...
#include "input.h"                       // for input_context
#include "item.h"                        // for item
#include "item_location.h"               // for item_location
#include "optional.h"                    // for optional, nullopt
#include "player_activity.h"             // for player_activity
#include "string_formatter.h"            // for string_format
#include "translations.h"                // for _
#include "ui.h"                          // for uilist, MENU_AUTOASSIGN
#include "ui_manager.h"                  // for ui_adaptor
#include "vpart_position.h"              // for vpart_reference
#include "transaction_ui.h"              // for transaction_ui, transaction_...
#include "uistate.h"                     // for uistate, uistatedata
#include "units.h"                       // for volume, operator""_liter, mass
#include "units_utility.h"               // for convert_weight, volume_units...
#include "veh_type.h"                    // for vpart_info
#include "vehicle.h"                     // for vehicle


extern activity_id const ACT_WEAR( "ACT_WEAR" );
extern activity_id const ACT_ADV_INVENTORY( "ACT_ADV_INVENTORY" );

namespace
{
using namespace advuilist_helpers;

constexpr std::size_t const aim_nsources = 18;
constexpr point const aimlayout{ 6, 3 };

constexpr char const SOURCE_DRAGGED_i = 'D';
constexpr char const SOURCE_VEHICLE_i = 'V';
constexpr char const *ACTION_EXAMINE = "EXAMINE";
constexpr char const *ACTION_ITEMS_DEFAULT = "ITEMS_DEFAULT";
constexpr char const *ACTION_SAVE_DEFAULT = "SAVE_DEFAULT";
constexpr char const *TOGGLE_AUTO_PICKUP = "TOGGLE_AUTO_PICKUP";
constexpr char const *TOGGLE_FAVORITE = "TOGGLE_FAVORITE";

#ifdef __clang__
#pragma clang diagnostic push
// travis' old clang wants a change that breaks compilation with newer versions
#pragma clang diagnostic ignored "-Wmissing-braces"
#endif

constexpr char const *_error = "error";
struct _aim_source_t {
    bool is_ground_source = false;
    char const *label = _error;
    char const *dir_abv = _error;
    aim_advuilist_sourced_t::icon_t icon = 0;
    tripoint offset;

};
using _sourcearray = std::array<_aim_source_t, aim_nsources>;
constexpr _sourcearray const aimsources = {
    _aim_source_t{ false, "Container", _error, 'C', tripoint_zero },
    _aim_source_t{ false, "Grabbed Vehicle", _error, SOURCE_DRAGGED_i, tripoint_zero },
    _aim_source_t{ false, _error, _error, 0, tripoint_zero },
    _aim_source_t{ true, "North West", "NW", '7', tripoint_north_west },
    _aim_source_t{ true, "North", "N", '8', tripoint_north },
    _aim_source_t{ true, "North East", "NE", '9', tripoint_north_east },
    _aim_source_t{ false, _error, _error, 0, tripoint_zero },
    _aim_source_t{ false, "Inventory", _error, 'I', tripoint_zero },
    _aim_source_t{ false, _error, _error, 0, tripoint_zero },
    _aim_source_t{ true, "West", "W", '4', tripoint_west },
    _aim_source_t{ true, "Directly below you", "DN", '5', tripoint_zero },
    _aim_source_t{ true, "East", "E", '6', tripoint_east },
    _aim_source_t{ false, "Surrounding area", _error, 'A', tripoint_zero },
    _aim_source_t{ false, "Worn Items", _error, 'W', tripoint_zero },
    _aim_source_t{ false, _error, _error, 0, tripoint_zero },
    _aim_source_t{ true, "South West", "SW", '1', tripoint_south_west },
    _aim_source_t{ true, "South", "S", '2', tripoint_south },
    _aim_source_t{ true, "South East", "SE", '3', tripoint_south_east },
};

#ifdef __clang__
#pragma clang diagnostic pop
#endif

constexpr _sourcearray::size_type const CONT_IDX = 0;
constexpr _sourcearray::size_type const DRAGGED_IDX = 1;
constexpr _sourcearray::size_type const INV_IDX = 7;
constexpr _sourcearray::size_type const ALL_IDX = 12;
constexpr _sourcearray::size_type const WORN_IDX = 13;
// this could be a constexpr too if we didn't have to use old compilers
tripoint slotidx_to_offset( aim_advuilist_sourced_t::slotidx_t idx )
{
    if( idx == DRAGGED_IDX ) {
        return get_avatar().grab_point;
    }

    return aimsources[idx].offset;
}

// this could be constexpr in C++20
_sourcearray::size_type offset_to_slotidx( tripoint const &off )
{
    _sourcearray::const_iterator const it =
    std::find_if( aimsources.begin(), aimsources.end(), [&]( _aim_source_t const & v ) {
        return v.is_ground_source and v.offset == off;
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

bool source_player_dragged_avail()
{
    avatar const &u = get_avatar();
    if( u.get_grab_type() == object_type::VEHICLE ) {
        return source_vehicle_avail( u.pos() + u.grab_point );
    }

    return false;
}

std::string aim_sourcelabel( _sourcearray::size_type idx, bool veh = false )
{
    _aim_source_t const &src = aimsources[idx];
    std::string const &srcname = src.label;

    tripoint const pos = get_avatar().pos() + slotidx_to_offset( idx );
    std::string prefix = srcname;
    std::string label;

    if( ( veh and source_vehicle_avail( pos ) ) or
        ( idx == DRAGGED_IDX and source_player_dragged_avail() ) ) {
        cata::optional<vpart_reference> vp = veh_cargo_at( pos );
        prefix = vp->vehicle().name;
        label = vp->get_label().value_or( vp->info().name() );
    } else {
        if( src.is_ground_source ) {
            label = get_map().name( pos );
        }
    }

    return string_format( "%s\n%s", colorize( prefix, c_green ), colorize( label, c_light_blue ) );
}
// end hacky stuff

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
    // FIXME: fugly
    aim_advuilist_sourced_t *otherpane = ui->left()->getSource().slotidx == ALL_IDX ? ui->right() :
                                         ui->left();
    getsource_t osrc = otherpane->getSource();
    if( osrc.slotidx == DRAGGED_IDX ) {
        osrc.slotidx = offset_to_slotidx( get_avatar().grab_point );
    }

    for( auto const &v : aimsources ) {
        if( v.is_ground_source ) {
            tripoint const off = v.offset;
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
    return aimsources[offset_to_slotidx( off )].dir_abv;
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

void aim_inv_idv_stats( aim_advuilist_sourced_t *ui )
{
    using select_t = aim_transaction_ui_t::select_t;
    select_t const peek = ui->peek();
    catacurses::window &w = *ui->get_window();
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

        right_print( w, 2, 2, c_white,
                     string_format( _( "INDV %s/%s %s  %s/%s %s" ), peek_len_str, indiv_len_cap_str,
                                    len_unit, peek_vol_str, indiv_vol_cap_str,
                                    volume_units_abbr() ) );
    }
}

void aim_inv_stats( aim_advuilist_sourced_t *ui )
{
    catacurses::window &w = *ui->get_window();
    avatar const &u = get_avatar();
    double const weight = convert_weight( u.weight_carried() );
    double const weight_cap = convert_weight( u.weight_capacity() );
    std::string const weight_str =
        colorize( string_format( "%.1f", weight ), weight >= weight_cap ? c_red : c_light_green );
    std::string const volume_cap = format_volume( u.volume_capacity() );
    std::string const volume = format_volume( u.volume_carried() );

    right_print( w, 1, 2, c_white,
                 string_format( "%s/%.1f %s  %s/%s %s", weight_str, weight_cap, weight_units(),
                                volume, volume_cap, volume_units_abbr() ) );
}

void aim_ground_veh_stats( aim_advuilist_sourced_t *ui, aim_stats_t *stats )
{
    using namespace advuilist_helpers;
    using getsource_t = aim_advuilist_sourced_t::getsource_t;
    getsource_t const src = ui->getSource();
    catacurses::window &w = *ui->get_window();
    units::volume vol_cap = 0_liter;
    tripoint const off = slotidx_to_offset( src.slotidx );
    tripoint const loc = get_avatar().pos() + off;

    if( src.icon == SOURCE_VEHICLE_i or src.slotidx == DRAGGED_IDX ) {
        cata::optional<vpart_reference> const vp = veh_cargo_at( loc );
        vol_cap = vp ? vp->vehicle().max_volume( vp->part_index() ) : 0_liter;
    } else {
        vol_cap = get_map().max_volume( loc );
    }

    double const weight = convert_weight( stats->mass );
    std::string const volume = format_volume( stats->volume );
    std::string const volume_cap = format_volume( vol_cap );

    right_print( w, 1, 2, c_white,
                 string_format( "%3.1f %s  %s/%s %s", weight, weight_units(), volume,
                                volume_cap, volume_units_abbr() ) );
}

void aim_default_columns( aim_advuilist_t *myadvuilist )
{
    using col_t = typename aim_advuilist_t::col_t;
    myadvuilist->setColumns( std::vector<col_t> { col_t{ "Name", iloc_entry_name, 16 },
                             col_t{ "amt", iloc_entry_count, 2 },
                             col_t{ "weight", iloc_entry_weight, 3 },
                             col_t{ "vol", iloc_entry_volume, 3 }
                                                },
                             false );
}

void aim_all_columns( aim_advuilist_t *myadvuilist )
{
    using col_t = typename aim_advuilist_t::col_t;
    myadvuilist->setColumns( std::vector<col_t> { col_t{ "Name", iloc_entry_name, 16 },
                             col_t{ "src", iloc_entry_src, 2 },
                             col_t{ "amt", iloc_entry_count, 2 },
                             col_t{ "weight", iloc_entry_weight, 3 },
                             col_t{ "vol", iloc_entry_volume, 3 }
                                                },
                             false );
}

void aim_stats_printer( aim_advuilist_t *ui, aim_stats_t *stats )
{
    aim_advuilist_sourced_t *_ui = reinterpret_cast<aim_advuilist_sourced_t *>( ui );
    using slotidx_t = aim_advuilist_sourced_t::slotidx_t;
    slotidx_t const src = _ui->getSource().slotidx;

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
        get_avatar().takeoff( *it.ptr->stack[0] );
    }
}

// FIXME: can I dedup this with get_selection_amount() ?
void player_drop( aim_transaction_ui_t::select_t const &sel, tripoint const pos, bool to_vehicle )
{
    std::vector<drop_or_stash_item_info> to_drop;
    for( auto const &it : sel ) {
        if( sel.size() > 1 and it.ptr->stack.front()->is_favorite ) {
            continue;
        }
        aim_advuilist_t::count_t count = it.count;
        if( it.ptr->stack.front()->count_by_charges() ) {
            to_drop.emplace_back( it.ptr->stack.front(), count );
        } else {
            for( auto v = it.ptr->stack.begin(); v != it.ptr->stack.begin() + count; ++v ) {
                to_drop.emplace_back( *v, count );
            }
        }
    }
    get_avatar().assign_activity( player_activity( drop_activity_actor( to_drop, pos, !to_vehicle ) ) );
}

void get_selection_amount( aim_transaction_ui_t::select_t const &sel,
                           std::vector<item_location> *targets, std::vector<int> *quantities,
                           bool ignorefav = false )
{
    bool const _ifav = sel.size() > 1 and ignorefav;
    for( auto const &it : sel ) {
        if( _ifav and it.ptr->stack.front()->is_favorite ) {
            continue;
        }
        if( it.ptr->stack.front()->count_by_charges() ) {
            targets->emplace_back( *it.ptr->stack.begin() );
            quantities->emplace_back( it.count );
        } else {
            targets->insert( targets->end(), it.ptr->stack.begin(),
                             it.ptr->stack.begin() + it.count );
            quantities->insert( quantities->end(), it.count, 0 );
        }
    }
}

void player_wear( aim_transaction_ui_t::select_t const &sel )
{
    avatar &u = get_avatar();
    u.assign_activity( ACT_WEAR );
    get_selection_amount( sel, &u.activity.targets, &u.activity.values );
}

void player_pick_up( aim_transaction_ui_t::select_t const &sel, bool from_vehicle )
{
    avatar &u = get_avatar();

    std::vector<item_location> targets;
    std::vector<int> quantities;
    get_selection_amount( sel, &targets, &quantities );

    u.assign_activity( player_activity( pickup_activity_actor(
                                            targets, quantities,
                                            from_vehicle ? cata::nullopt : cata::optional<tripoint>( u.pos() ) ) ) );
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
    if( ui->getSource().slotidx == ALL_IDX ) {
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
        tripoint const off = v.offset;
        if( idx != ALL_IDX and v.is_ground_source ) {
            bool const valid = source_player_ground_avail( off );
            menu.addentry( idx, valid, MENU_AUTOASSIGN, v.label );
        }
        idx++;
    }
    menu.query();
    return menu.ret;
}

bool swap_panes_maybe( aim_transaction_ui_t *ui )
{
    using namespace advuilist_literals;
    using getsource_t = aim_advuilist_sourced_t::getsource_t;
    getsource_t const psrc = ui->curpane()->getSourcePrev();
    getsource_t const csrc = ui->curpane()->getSource();
    getsource_t const osrc = ui->otherpane()->getSource();
    // swap panes if the requested source is already selected in the other pane
    // also swap panes if the current source is re-selected since people have grown accustomed
    // to this behaviour (see discussion in #45900)
    if( csrc.avail and osrc.avail and
        ( ( csrc.slotidx == osrc.slotidx and csrc.icon == osrc.icon ) or
          ( psrc.slotidx == csrc.slotidx and psrc.icon == csrc.icon ) or
          ( is_dragged( csrc ) and is_dragged( osrc ) ) ) ) {

        ui->curpane()->setSource( osrc.slotidx, osrc.icon, false, false );
        ui->otherpane()->setSource( psrc.slotidx, psrc.icon, false, false );

        change_columns( ui->otherpane() );
        return true;
    }

    return false;
}

void aim_rebuild( aim_transaction_ui_t *ui )
{
    ui->left()->rebuild();
    ui->right()->rebuild();
}

void setup_for_aim( aim_advuilist_t *myadvuilist, aim_stats_t *stats )
{
    using sorter_t = typename aim_advuilist_t::sorter_t;
    using grouper_t = typename aim_advuilist_t::grouper_t;
    using filter_t = typename aim_advuilist_t::filter_t;

    aim_default_columns( myadvuilist );
    myadvuilist->setcountingf( iloc_entry_counter );
    // we need to replace name sorter due to color tags
    myadvuilist->addSorter( sorter_t{ "Name", iloc_entry_name_sorter } );
    // use numeric sorters instead of advuilist's lexicographic ones
    myadvuilist->addSorter( sorter_t{ "amount", iloc_entry_count_sorter } );
    myadvuilist->addSorter( sorter_t{ "weight", iloc_entry_weight_sorter } );
    myadvuilist->addSorter( sorter_t{ "volume", iloc_entry_volume_sorter } );
    // extra sorters
    myadvuilist->addSorter( sorter_t{ "damage", iloc_entry_damage_sorter} );
    myadvuilist->addSorter( sorter_t{ "spoilage", iloc_entry_spoilage_sorter} );
    myadvuilist->addSorter( sorter_t{ "price", iloc_entry_price_sorter} );
    myadvuilist->addGrouper( grouper_t{ "category", iloc_entry_gsort, iloc_entry_glabel } );
    // FIXME: this string is duplicated from draw_item_filter_rules() because that function doesn't fit
    // anywhere in the current implementation of advuilist
    std::string const desc = string_format(
                                 "%s\n\n%s\n %s\n\n%s\n %s\n\n%s\n %s", _( "Type part of an item's name to filter it." ),
                                 _( "Separate multiple items with [<color_yellow>,</color>]." ), // NOLINT(cata-text-style): literal comma
                                 _( "Example: back,flash,aid, ,band" ), // NOLINT(cata-text-style): literal comma
                                 _( "To exclude items, place [<color_yellow>-</color>] in front." ),
                                 _( "Example: -pipe,-chunk,-steel" ),
                                 _( "Search [<color_yellow>c</color>]ategory, [<color_yellow>m</color>]aterial, "
                                    "[<color_yellow>q</color>]uality, [<color_yellow>n</color>]otes or "
                                    "[<color_yellow>d</color>]isassembled components." ),
                                 _( "Examples: c:food,m:iron,q:hammering,n:toolshelf,d:pipe" ) );
    myadvuilist->setfilterf( filter_t{ desc, iloc_entry_filter } );
    myadvuilist->on_rebuild(
    [stats]( bool first, iloc_entry const & it ) {
        iloc_entry_stats( stats, first, it );
    } );
    myadvuilist->on_redraw(
    [stats]( aim_advuilist_t *ui ) {
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

    // Cataclysm: Hacky Stuff Redux
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
                    _fs = [ = ]() {
                        return source_player_ground( src.offset );
                    };
                    _fsb = [ = ]() {
                        return source_player_ground_avail( src.offset );
                    };
                    _fsv = [ = ]() {
                        return source_player_vehicle( src.offset );
                    };
                    _fsvb = [ = ]() {
                        return source_player_vehicle_avail( src.offset );
                    };
                    break;
                }
            }
            flabel_t const label = [ = ]() {
                return aim_sourcelabel( idx );
            };
            myadvuilist->addSource( idx, source_t{ label, src.icon, _fs, _fsb } );
            if( _fsv ) {
                flabel_t const vlabel = [ = ]() {
                    return aim_sourcelabel( idx, true );
                };
                myadvuilist->addSource( idx, source_t{ vlabel, SOURCE_VEHICLE_i, _fsv, _fsvb } );
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
    getsource_t const csrc = ui->curpane()->getSource();
    getsource_t dst = ui->otherpane()->getSource();

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

    // close the transaction_ui so that player activities can run
    ui->pushevent( aim_transaction_ui_t::event::QUIT );
}

// FIXME: fragment this as it has grown quite large
void aim_ctxthandler( aim_transaction_ui_t *ui, std::string const &action )
{
    using namespace advuilist_literals;
    using select_t = aim_transaction_ui_t::select_t;
    using slotidx_t = aim_advuilist_sourced_t::slotidx_t;
    select_t const peek = ui->curpane()->peek();

    if( action == ACTION_CYCLE_SOURCES or
        action.substr( 0, ACTION_SOURCE_PRFX_len ) == ACTION_SOURCE_PRFX ) {

        bool const swapped = swap_panes_maybe( ui );

        change_columns( ui->curpane() );
        // rebuild other pane if it's set to the ALL source
        if( swapped or ui->otherpane()->getSource().slotidx == ALL_IDX ) {
            ui->otherpane()->rebuild();
            ui->otherpane()->get_ui()->invalidate_ui();
        }

    } else if( action == ACTION_SAVE_DEFAULT ) {
        ui->savestate( &uistate.transfer_default );

    } else if( action == ACTION_ITEMS_DEFAULT ) {
        ui->curpane()->suspend();
        ui->loadstate( &uistate.transfer_default, false );
        aim_rebuild( ui );
        ui->otherpane()->get_ui()->invalidate_ui();

    } else if( !peek.empty() ) {
        iloc_entry &entry = *peek.front().ptr;

        if( action == ACTION_EXAMINE ) {
            slotidx_t const src = ui->curpane()->getSource().slotidx;
            if( src == INV_IDX or src == WORN_IDX ) {
                aim_add_return_activity();
                ui->pushevent( aim_transaction_ui_t::event::QUIT );
                ui->curpane()->suspend();
                ui->curpane()->hide();
                ui->otherpane()->hide();

                std::pair<point, point> const dim = ui->otherpane()->get_size();
                game::inventory_item_menu_position const side =
                    ui->curpane() == ui->left() ? game::LEFT_OF_INFO : game::RIGHT_OF_INFO;
                g->inventory_item_menu(
                    entry.stack.front(), [ = ] { return dim.second.x; }, [ = ] { return dim.first.x; },
                    side );
            } else {
                iloc_entry_examine( ui->otherpane()->get_window(), entry );
            }

        } else if( action == TOGGLE_AUTO_PICKUP ) {
            item const *it = entry.stack.front().get_item();
            bool const has = get_auto_pickup().has_rule( it );
            if( !has ) {
                get_auto_pickup().add_rule( it );
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

} // namespace

void create_advanced_inv( bool resume )
{
    using mytrui_t = transaction_ui<aim_container_t>;

    static std::unique_ptr<mytrui_t> mytrui;
    static aim_stats_t lstats{ 0_kilogram, 0_liter };
    static aim_stats_t rstats{ 0_kilogram, 0_liter };
    static bool full_screen{ get_option<bool>( "AIM_WIDTH" ) };
    bool const _fs = get_option<bool>( "AIM_WIDTH" );
    if( !mytrui ) {
        std::pair<point, point> const size = aim_size( full_screen );

        mytrui = std::make_unique<mytrui_t>( aimlayout, size.first, size.second,
                                             "ADVANCED_INVENTORY", point{3, 1} );
        mytrui->on_resize( [&]( mytrui_t *ui ) {
            std::pair<point, point> size = aim_size( full_screen );
            ui->resize( size.first, size.second );
        } );

        setup_for_aim( mytrui->left(), &lstats );
        setup_for_aim( mytrui->right(), &rstats );
        add_aim_sources( mytrui->left(), mytrui.get() );
        add_aim_sources( mytrui->right(), mytrui.get() );
        mytrui->on_select( aim_transfer );
        mytrui->setctxthandler( [&]( aim_transaction_ui_t *ui, std::string const & action ) {
            aim_ctxthandler( ui, action );
        } );
        mytrui->loadstate( &uistate.transfer_save, false );

    } else if( full_screen != _fs ) {
        full_screen = _fs;
        std::pair<point, point> const size = aim_size( full_screen );
        mytrui->resize( size.first, size.second );

    }

    if( !resume and get_option<bool>( "OPEN_DEFAULT_ADV_INV" ) ) {
        mytrui->loadstate( &uistate.transfer_default, false );
    } else {
        mytrui->loadstate( &uistate.transfer_save, false );
    }

    aim_rebuild( &*mytrui );

    mytrui->show();
    mytrui->savestate( &uistate.transfer_save );
}
