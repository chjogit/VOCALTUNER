#include "DocumentController.h"
#include <algorithm>
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

bool VocalTunerDocumentController::doRestoreObjectsFromStream(juce::ARAInputStream&,
                                                              const juce::ARARestoreObjectsFilter*)
{
    return true;
}

bool VocalTunerDocumentController::doStoreObjectsToStream(juce::ARAOutputStream&,
                                                          const juce::ARAStoreObjectsFilter*)
{
    return true;
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
