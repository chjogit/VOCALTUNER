#include "PianoRollView.h"

// 키보드 ←/→: 선택한 블록에서 이전/다음 블록으로 옮기고, 옮긴 블록의 소리를 한 번 들려준다.

std::optional<size_t> PianoRollView::neighbourBlock(size_t from, int direction) const
{
    const auto& blocks = *content.blocks;
    for (auto i = static_cast<long>(from) + direction; i >= 0 && i < static_cast<long>(blocks.size()); i += direction)
    {
        const auto& b = blocks[static_cast<size_t>(i)];
        if ((toolPicksConsonants() || ! b.isConsonant) && inRegion(b))
            return static_cast<size_t>(i);
    }
    return std::nullopt;
}

// 블록이 화면 밖이면 보이도록 옮긴다 (가로만)
void PianoRollView::revealBlock(const VocalBlock& b)
{
    const auto start = sourceToSong(b.displayStartSec()), end = sourceToSong(b.displayEndSec());
    const auto margin = 0.1 * (viewport.endSec() - viewport.startSec);
    if (start < viewport.startSec)
        viewport.startSec = start - margin;
    else if (end > viewport.endSec())
        viewport.startSec += end - viewport.endSec() + margin;
}

bool PianoRollView::stepSelection(int direction)
{
    if (content.blocks == nullptr || selection.empty() || drag.kind != DragKind::None)
        return false;
    const auto from = primaryBlock.value_or(direction < 0 ? selection.front() : selection.back());
    const auto next = neighbourBlock(from, direction);
    if (! next)
        return true;    // 끝: 키는 그대로 받아 둔다 (호스트로 넘기지 않음)
    selectOnly(*next);
    revealBlock((*content.blocks)[*next]);
    repaint();
    if (onPreviewOnce)
        onPreviewOnce(*next);
    return true;
}
