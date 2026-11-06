#include "DocumentController.h"
#include <algorithm>
#include <string>
#include <vector>
#include "PlaybackRenderer.h"

juce::ARAAudioSource* VocalTunerDocumentController::doCreateAudioSource(juce::ARADocument* document,
                                                                        ARA::ARAAudioSourceHostRef hostRef)
{
    auto* source = ARADocumentControllerSpecialisation::doCreateAudioSource(document, hostRef);
    analysis.track(*source);
    renderCache.track(*source);
    return source;
}

juce::ARAPlaybackRenderer* VocalTunerDocumentController::doCreatePlaybackRenderer() noexcept
{
    return new PlaybackRenderer(getDocumentController(), processingLock, renderCache, preview);
}

namespace
{
constexpr int archiveMagic = 'V' | ('T' << 8) | ('D' << 16) | ('C' << 24);   // "VTDC"
constexpr int archiveVersion = 1;
constexpr juce::int64 unknownLengthLimit = 256 * 1024 * 1024;

struct ArchivedSource
{
    std::string id;
    std::vector<uint8_t> state;
};

bool writeBytes(juce::OutputStream& out, const void* data, size_t size)
{
    return out.writeInt(static_cast<int>(size)) && (size == 0 || out.write(data, size));
}

// 길이를 남은 바이트와 대조한 뒤에야 할당한다
bool readBytes(juce::InputStream& in, std::vector<uint8_t>& bytes)
{
    const auto size = in.readInt();
    const auto remaining = in.getNumBytesRemaining();
    if (size < 0 || static_cast<juce::int64>(size) > (remaining >= 0 ? remaining : unknownLengthLimit))
        return false;
    bytes.resize(static_cast<size_t>(size));
    return size == 0 || in.read(bytes.data(), size) == size;
}

bool readId(juce::InputStream& in, std::string& id)
{
    std::vector<uint8_t> bytes;
    if (! readBytes(in, bytes))
        return false;
    id.assign(bytes.begin(), bytes.end());
    return true;
}

const std::vector<juce::ARAAudioSource*>& documentSources(ARA::PlugIn::DocumentController* controller)
{
    static const std::vector<juce::ARAAudioSource*> none;
    auto* document = controller != nullptr ? controller->getDocument() : nullptr;
    return document != nullptr ? document->getAudioSources<juce::ARAAudioSource>() : none;
}

bool isStored(const juce::ARAStoreObjectsFilter* filter, const juce::ARAAudioSource* source)
{
    if (filter == nullptr)
        return true;
    for (size_t i = 0; i < filter->getAudioSourcesToStoreCount(); ++i)
        if (filter->getAudioSourceToStoreAtIndex<juce::ARAAudioSource>(i) == source)
            return true;
    return false;
}

juce::ARAAudioSource* findSource(ARA::PlugIn::DocumentController* controller, const juce::ARARestoreObjectsFilter* filter,
                                 const std::string& id)
{
    if (filter != nullptr)
        return filter->getAudioSourceToRestoreStateWithID<juce::ARAAudioSource>(id.c_str());
    for (auto* source : documentSources(controller))
        if (source->getPersistentID() != nullptr && id == source->getPersistentID())
            return source;
    return nullptr;
}

// 항목 count개를 읽어 맞는 소스에 맡긴다. 못 찾는 id는 건너뜀. 읽기 실패면 false
bool restoreEntries(juce::InputStream& input, int count, AnalysisManager& analysis,
                    ARA::PlugIn::DocumentController* controller, const juce::ARARestoreObjectsFilter* filter)
{
    for (int i = 0; i < count; ++i)
    {
        ArchivedSource archived;
        if (! readId(input, archived.id) || ! readBytes(input, archived.state))
            return false;
        if (auto* source = findSource(controller, filter, archived.id))
            analysis.setRestoredState(source, std::move(archived.state));
    }
    return true;
}
} // namespace

bool VocalTunerDocumentController::doRestoreObjectsFromStream(juce::ARAInputStream& input,
                                                              const juce::ARARestoreObjectsFilter* filter)
{
    if (input.isExhausted())
        return true;   // 저장 기능이 없던 버전의 빈 아카이브
    const auto magic = input.readInt();
    const auto version = input.readInt();
    const auto count = input.readInt();
    if (input.failed())
        return false;
    if (magic != archiveMagic || version != archiveVersion)
        return true;   // 알 수 없는 형식은 건너뜀 (분석 결과만 쓴다)

    if (! restoreEntries(input, count, analysis, getDocumentController(), filter))
        return false;
    return ! input.failed();
}

bool VocalTunerDocumentController::doStoreObjectsToStream(juce::ARAOutputStream& output,
                                                          const juce::ARAStoreObjectsFilter* filter)
{
    std::vector<ArchivedSource> archive;
    for (auto* source : documentSources(getDocumentController()))
        if (source->getPersistentID() != nullptr && isStored(filter, source))
            if (auto state = analysis.getSavedState(source))
                archive.push_back({ source->getPersistentID(), std::move(*state) });

    bool ok = output.writeInt(archiveMagic) && output.writeInt(archiveVersion)
              && output.writeInt(static_cast<int>(archive.size()));
    for (const auto& item : archive)
        ok = ok && writeBytes(output, item.id.data(), item.id.size())
             && writeBytes(output, item.state.data(), item.state.size());
    return ok;
}

const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return juce::ARADocumentControllerSpecialisation::createARAFactory<VocalTunerDocumentController>();
}

bool VocalTunerDocumentController::requestHostPlaybackPosition(double songSec)
{
    auto* host = getDocumentController()->getHostPlaybackController();
    if (host == nullptr)
        return false;
    host->requestSetPlaybackPosition(std::max(0.0, songSec));
    return true;
}

bool VocalTunerDocumentController::requestHostPlayback(bool start)
{
    auto* host = getDocumentController()->getHostPlaybackController();
    if (host == nullptr)
        return false;
    if (start)
        host->requestStartPlayback();
    else
        host->requestStopPlayback();
    return true;
}
