#pragma once
#include <optional>
#include "pager.h"

class Replacer {
public:
    virtual ~Replacer() = default;

    // id에 접근했음을 알림. 처음 보는 id면 추적을 시작하고,
    // 그 때문에 수용량을 넘으면 내보낼 id를 반환.
    // 규칙: 방금 touch한 id는 반환하지 않는다. (설계상 이유)
    virtual std::optional<page_id_t> touch(page_id_t id) = 0;
};