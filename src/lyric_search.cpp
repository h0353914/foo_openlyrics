#include "stdafx.h"

#include "logging.h"
#include "lyric_data.h"
#include "lyric_search.h"
#include "parsers.h"
#include "sources/lyric_source.h"
#include "sources/localfiles.h"
#include "winstr_util.h"

LyricSearch::LyricSearch(metadb_handle_ptr track) :
    m_track(track),
    m_mutex({}),
    m_lyrics(nullptr),
    m_abort(),
    m_complete(nullptr)
{
    InitializeCriticalSection(&m_mutex);
    m_complete = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    assert(m_complete != nullptr);

    fb2k::splitTask([this](){
        run_async();
    });
}

LyricSearch::~LyricSearch()
{
    if(!m_abort.is_aborting())
    {
        m_abort.abort();
    }

    DWORD wait_result = WaitForSingleObject(m_complete, 30'000);
    if(wait_result != WAIT_OBJECT_0)
    {
        LOG_ERROR("Lyric search did not complete successfully during cleanup: %d", wait_result);
    }
    CloseHandle(m_complete);
    DeleteCriticalSection(&m_mutex);

    if(m_lyrics != nullptr)
    {
        delete m_lyrics;
    }
}

LyricData* LyricSearch::get_result()
{
    EnterCriticalSection(&m_mutex);
    LyricData* result = m_lyrics;
    LeaveCriticalSection(&m_mutex);
    return result;
}

static void ensure_windows_newlines(std::string& str)
{
    int replace_count = 0;
    size_t prev_index = 0;
    while(true)
    {
        size_t next_index = str.find('\n', prev_index);
        if(next_index == pfc::infinite_size)
        {
            break;
        }

        if((next_index == 0) || (str[next_index-1] != '\r'))
        {
            char cr = '\r';
            str.insert(next_index, 1, cr);
            replace_count++;
        }

        prev_index = next_index+1;
    }
}

void LyricSearch::run_async()
{
    TRACK_CALL_TEXT("Search for lyrics");
    LyricData* lyric_data = new LyricData();

    // TODO: Return a progress percentage while searching, and show "Searching: 63%" along with a visual progress bar
    LyricSourceBase* success_source = nullptr;
    LyricDataRaw lyric_data_raw = {};
    for(GUID source_id : preferences::get_active_sources())
    {
        LyricSourceBase* source = LyricSourceBase::get(source_id);
        assert(source != nullptr);

        // TODO: Only load files if the file that gets loaded has a newer timestamp than the existing one
        try
        {
            m_abort.check();

            lyric_data_raw = source->query(m_track, m_abort);
            if(!lyric_data_raw.text.empty())
            {
                success_source = source;
                LOG_INFO("Successfully retrieved lyrics from source: %s", tchar_to_string(source->friendly_name()).c_str());
                break;
            }
        }
        catch(const std::exception& e)
        {
            LOG_ERROR("Error while searching %s: %s", tchar_to_string(source->friendly_name()).c_str(), e.what());
        }
        catch(...)
        {
            LOG_ERROR("Error of unrecognised type while searching %s", tchar_to_string(source->friendly_name()));
        }

        LOG_INFO("Failed to retrieve lyrics from source: %s", tchar_to_string(source->friendly_name()).c_str());
    }
    ensure_windows_newlines(lyric_data_raw.text);

    if(!lyric_data_raw.text.empty())
    {
        LOG_INFO("Parsing lyrics as LRC...");
        *lyric_data = parsers::lrc::parse(lyric_data_raw);
    }

    try
    {
        if(!lyric_data->IsEmpty() && preferences::get_autosave_enabled() &&
           (success_source != nullptr) && !success_source->is_local())
        {
            SaveMethod method = preferences::get_save_method();
            switch(method)
            {
                case SaveMethod::ConfigDirectory:
                {
                    // TODO: This save triggers an immediate reload from the directory watcher.
                    //       This is not *necessarily* a problem, but it is some unnecessary work
                    //       and it means that we immediately lose the source information for
                    //       downloaded lyrics.
                    if(lyric_data->IsTimestamped())
                    {
                        std::string shrunk_text = parsers::lrc::shrink_text(*lyric_data);
                        sources::localfiles::SaveLyrics(m_track, true, shrunk_text, m_abort);
                    }
                    else
                    {
                        sources::localfiles::SaveLyrics(m_track, false, lyric_data->text, m_abort);
                    }
                } break;

                case SaveMethod::Id3Tag:
                {
                    // TODO: Add saving to ID3 tag
                    LOG_WARN("Saving lyrics to file tags is not currently supported");
                    assert(false);
                } break;

                case SaveMethod::None: break;

                default:
                    LOG_WARN("Unrecognised save method: %d", (int)method);
                    assert(false);
                    break;
            }
        }
    }
    catch(const std::exception& e)
    {
        LOG_ERROR("Failed to download lyrics: %s", e.what());
    }

    EnterCriticalSection(&m_mutex);
    m_lyrics = lyric_data;
    LeaveCriticalSection(&m_mutex);

    BOOL complete_success = SetEvent(m_complete);
    assert(complete_success);
    LOG_INFO("Lyric loading complete");
}
