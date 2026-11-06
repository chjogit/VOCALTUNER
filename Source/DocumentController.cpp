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

std::vector<juce::ARAAudioSource*> documentSources(ARA::PlugIn::DocumentController* controller)
{
    auto* document = controller != nullptr ? controller->getDocument() : nullptr;
    if (document == nullptr)
        return {};
    return document->getAudioSources<juce::ARAAudioSource>();   // 값으로 복사
}

// 저장 대상 소스 목록: 필터가 없으면 문서의 모든 소스
std::vector<juce::ARAAudioSource*> sourcesToStore(ARA::PlugIn::DocumentController* controller,
                                                  const juce::ARAStoreObjectsFilter* filter)
{
    if (filter != nullptr)
    {
        // 필터는 const 포인터를 준다. 객체 자체는 문서 소유(비 const)라 const_cast가 안전하다.
        std::vector<juce::ARAAudioSource*> sources;
        for (const auto* source : filter->getAudioSourcesToStore<juce::ARAAudioSource>())
            sources.push_back(const_cast<juce::ARAAudioSource*>(source));
        return sources;
    }
    return documentSources(controller);
}

juce::ARAAudioSource* findSource(ARA::PlugIn::DocumentController* controller, const juce::ARARestoreObjectsFilter* filter,
                                 const std::string& id)
{
    if (filter != nullptr)
        return filter->getAudioSourceToRestoreStateWithID<juce::ARAAudioSource>(id.c_str());
    for (auto* source : documentSources(controller))
        if (! source->getPersistentID().empty() && id == source->getPersistentID())
            return source;
    return nullptr;
}

// 항목 count개를 모두 읽는다. 하나라도 실패하면 false (아무것도 적용하지 않기 위해 읽기와 적용을 나눔)
bool readEntries(juce::InputStream& input, int count, std::vector<ArchivedSource>& entries)
{
    if (count < 0)
        return false;
    for (int i = 0; i < count; ++i)
    {
        ArchivedSource archived;
        if (! readId(input, archived.id) || ! readBytes(input, archived.state))
            return false;
        entries.push_back(std::move(archived));
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

    std::vector<ArchivedSource> entries;
    if (! readEntries(input, count, entries) || input.failed())
        return false;
    for (auto& archived : entries)   // 전부 읽은 뒤에만 적용. 못 찾는 id는 건너뜀
        if (auto* source = findSource(getDocumentController(), filter, archived.id))
            analysis.setRestoredState(source, std::move(archived.state));
    return true;
}

bool VocalTunerDocumentController::doStoreObjectsToStream(juce::ARAOutputStream& output,
                                                          const juce::ARAStoreObjectsFilter* filter)
{
    std::vector<ArchivedSource> archive;
    for (auto* source : sourcesToStore(getDocumentController(), filter))
        if (! source->getPersistentID().empty())
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
