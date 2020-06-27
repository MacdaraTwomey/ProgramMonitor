#pragma once

#include "date.h"
#include "graphics.h"
#include "helper.h"
#include "monitor_string.h"

#include <unordered_map>
#include <chrono>

// TODO: Think of better way than just having error prone max amounts.
static constexpr u32 MaxDailyRecords = 1000;
static constexpr u32 MaxDays = 1000; // temporary
static constexpr u32 DefaultDayAllocationCount = 30;
static constexpr i32 MaxWebsiteCount = 50; // this used?
static constexpr i32 MAX_KEYWORD_COUNT = 100;
static constexpr i32 MAX_KEYWORD_SIZE = 101;

static constexpr i32 MICROSECS_PER_SEC = 1000000;

// u32 can overflows after 50 days when usning milliseconds, this might be ok
// as we only get this when summing multiple days, but for now KISS.
typedef s64 time_type;
typedef u32 App_Id;

// Steady clock typically uses system startup time as epoch, and system clock uses systems epoch like 1970-1-1 00:00
// Clocks have a starting point (epoch) and tick rate (e.g. 1 tick per second)
// A Time Point is a duration of time that has passed since a clocks epoch
// A Duration consists of a span of time, defined as a number of ticks of some time unit (e.g. 12 ticks in millisecond unit)
// On windows Steady clock is based on QueryPerformanceCounter
//using Steady_Clock = std::chrono::steady_clock;
using Steady_Clock = std::chrono::steady_clock;
using System_Clock = std::chrono::system_clock;

struct Header
{
    // u32 version; // version 0
};

// custom specialization of std::hash can be injected in namespace std
namespace std
{
    template<> struct std::hash<String>
    {
        std::size_t operator()(String const& s) const noexcept
        {
            size_t hash = djb2((unsigned char *)s.str, s.length);
            return hash;
        }
    };
    
    template <> struct std::equal_to<String>
    {
        bool operator()(String const& a, String const& b) const noexcept
        {
            return string_equals(a, b);
        }
    };
    
    template<> struct std::hash<App_Id>
    {
        std::size_t operator()(App_Id const& id) const noexcept
        {
            // Probably fine for program ids (start at 1)
            return (size_t)id;
        }
    };
    
}

enum Record_Type
{
	Record_Invalid,
	Record_Exe,     // Start at 0
	Record_Firefox, // Start at 0x800000
};
struct Record
{
    // Id could be made 64 bit and record would be the same size
    App_Id id;
    time_type duration; // microseconds
};

// Days (and records) are:
//  - append only
//  - only last day modified
//  - saved to file
//  - any day may be checked up on
//  - contiguous set is iterated over
//  - new set of days is merged in

// Biggest difficulties may be:
// - serialising, where each days pointer will have to be relatived to its corresponding
//   block to create an overall record index (this is made easier by the fact that days are sequential)
//      - Could maybe add blocks to the start of the list, so it goes from newest to oldest (and same for file)

// NOTE: When deserialising may just want to put all records into one big block
// and then just append normal sized blocks during runtime.
// This probably means blocks must have a block_size field, because they can be variable size

static constexpr u32 BLOCK_SIZE = 1024 * sizeof(Record); // 16384
static constexpr u32 BLOCK_MAX_RECORDS = 1023;

struct Day
{
    Record *records;
    date::sys_days date;
    u32 record_count;
};
struct Block
{
    Record records[BLOCK_MAX_RECORDS];
    Block *next;
    u32 count;
    bool32 full; // needed? because we imediately start a new block when this would be set to true
    //u32 padding;
};
struct Day_List
{
    Block *blocks;
    std::vector<Day> days;
};
struct Day_View
{
    std::vector<Day> days;
    
    // Start and end dates can be out of range of actual stored days
    // the indexes point to the closest actual stored days
    // start_date < == > days[start_index].date  
    // end_date < == > days[end_index].date
    date::sys_days start_date;
    date::sys_days end_date;
    i32 start_range;
    i32 end_range;
    bool has_days;
    Record *copy_of_current_days_records;
};



static_assert(BLOCK_SIZE == sizeof(Block), "");




struct Keyword
{
    // Null terminated
    String str;
    App_Id id;
};

struct App_Info
{
    // TODO: Check that full paths saved to file are valid, and update if possible.
    // (full url, keyword) or
    // (path, exe name)
    
    String long_name; // this must be null terminated because passed to curl as url or OS as a path
    String short_name;
    
    // I don't really want this here
    i32 icon_index;   // -1 means not loaded
};

struct Icon_Asset
{
    // how to get back to icon_index if this is deleted
    
    // Do I even need to keep CPU side textures around after giving to GPU
    Bitmap bitmap;
    u32 texture_handle;
};

struct Database
{
	// Contains local programs only
    // This is used to quickly Assigned_Idable paths -> ID
    // Don't need a corresponding one for websites as we have to test agains all keywords anyway
    std::unordered_map<String, App_Id> id_table;
    
    // Contains websites and local programs
    // We use long_name as a path to load icons from executables
    // We use long_nameAssigned_Idto download favicon from website
    // We use shortname when we iterate records and want to display names
    std::unordered_map<App_Id, App_Info> app_names;
    
    // dont like passing database to give this to the settings code
    // 0 is illegal
    App_Id next_program_id;      // starts at 0x00000000 1
    App_Id next_website_id;      // starts at 0x80000000 top bit set
    
    // Temporary, maybe
    App_Id firefox_id;
    bool added_firefox;
    
    Day_List day_list;
    //Day_View day_view;
    
    // Can have:
    // - a path (updated or not) with no corresponding bitmap (either not loaded or unable to be loaded)
    // - a path (updated or not) with a bitmap
    //Bitmap icons[200]; // Loaded on demand
    //Bitmap website_icons[200]; // Loaded on demand
    
    // loaded at startup
    i32 default_icon_index;
    
    u32 icon_count;
    Icon_Asset icons[200];
};


struct Misc_Options
{
    // TODO: These may want to be different datatypes (tradeoff file IO ease vs imgui datatypes conversion)
    
    // In minute of the day 0-1439
    u16 day_start_time;  // Changing this won't trigger conversion of previously saved records
    u16 poll_start_time; // Default 0 (12:00AM) (if start == end, always poll)
    u16 poll_end_time;   // Default 0 (12:00AM)
    
    bool16 run_at_system_startup;   
    u32 poll_frequency_microseconds;   
    
};

struct Edit_Settings
{
    // This array can have blank strings representing empty input boxes in between valid strings
    char pending[MAX_KEYWORD_COUNT][MAX_KEYWORD_SIZE];
    s32 input_box_count;
    Misc_Options misc_options;
    int day_start_time_item;
    int poll_start_time_item;
    int poll_end_time_item;
    bool update_settings;
};

struct Settings
{
    std::vector<Keyword> keywords;
    Misc_Options misc_options;
};

struct
Monitor_State
{
    bool is_initialised;
    Header header;
    Database database;
    
    Settings settings;
    Edit_Settings *edit_settings; // allocated when needed
    
    time_type accumulated_time;
    
    // debug temporary
    time_type total_runtime;
    LARGE_INTEGER startup_time;
    s32 extra_days;
};
