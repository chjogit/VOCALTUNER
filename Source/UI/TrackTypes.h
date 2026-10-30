#pragma once

// 트랙(클립)이 화면에 나타나는 방식 — 왼쪽 메뉴와 피아노롤이 공유한다.
enum class TrackView
{
    Editable,   // 선명하게 보이고 클릭해서 편집
    Visible,    // 반투명(50%)에 채도 10%, 편집 불가
    Hidden      // 화면에서 사라지고 메뉴에만 남음
};
