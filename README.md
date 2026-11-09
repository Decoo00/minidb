# minidb

C++17로 처음부터 구현하는 디스크 기반 storage engine입니다.
고정 크기 page 단위 파일 I/O 위에 B+tree index를 올리고, buffer pool로 디스크 접근을 줄인 뒤, WAL 기반 crash recovery까지 단계적으로 확장하는 것을 목표로 합니다.

## 현재 구현 범위

| 단계 | 내용 | 상태 |
|---|---|---|
| M1 | Pager: 4096B page 단위 파일 I/O (`pread`/`pwrite`/`fsync`) | 완료 |
| M2 | 메모리 B+tree: 삽입, 검색, 범위 검색, 불변식 검사 | 완료 |
| M3 | 디스크 B+tree: 노드 직렬화, 메타 page, 재오픈 시 영속성 | 완료 |
| M4 | Buffer pool: 교체 정책 분리(LRU), write-back, 크기별 벤치마크 | 완료 |
| M5 | WAL과 crash recovery | 계획 |

## 구조

```
src/
├── page_store.h                 page 저장소 인터페이스 (Pager, BufferPool 공통)
├── pager.h / pager.cpp          page 번호 ↔ 파일 오프셋 변환, 읽기/쓰기, 디스크 I/O 횟수 집계
├── replacer.h                   page 교체 정책 인터페이스
├── lru.h / lru.cpp              LRU 교체 정책
├── buffer_pool.h / .cpp         page 캐시: hit/miss, dirty page write-back, flush
├── node.h / node.cpp            B+tree 노드 ↔ 4096B page 직렬화
└── bptree.h / bptree.cpp        page 기반 B+tree
tests/
├── pager_test.cpp               page 읽기/쓰기, 재오픈, 범위 밖 접근
├── lru_test.cpp                 교체 순서, 단순 구현과의 무작위 대조
├── buffer_pool_test.cpp         hit/miss, eviction 시 write-back, flush, 무작위 대조
├── bptree_test.cpp              std::map 대조 테스트, 재오픈 후 일관성 (buffer pool 위에서 실행)
├── io_count_test.cpp            연산별 page I/O 횟수를 이론값과 비교
└── bench_pool.cpp               buffer pool 크기별 디스크 I/O 측정
```

## 설계

### 계층

```
BPlusTree ──▶ PageStore (interface)
                ├── Pager        파일에 직접 읽기/쓰기
                └── BufferPool ──▶ Pager
                        └──▶ Replacer (interface)
                                └── LRU
```

- `BPlusTree`는 `PageStore` 인터페이스에만 의존합니다. 따라서 같은 트리를 buffer pool 없이 `Pager` 위에서, 또는 `BufferPool` 위에서 그대로 실행할 수 있으며, 벤치마크는 이 성질을 이용해 두 경우를 비교합니다.
- `BufferPool`은 어떤 page를 내보낼지를 `Replacer` 인터페이스에 위임합니다. 교체 정책은 외부에서 생성해 소유권과 함께 넘기므로, 정책을 추가해도 `BufferPool` 코드는 바뀌지 않습니다.

### 파일 레이아웃
- 파일은 4096B page의 배열이며, page `n`은 파일 오프셋 `n * 4096`에 위치합니다.
- page 0은 메타 page입니다. magic 문자열 `Hello DB`, root page 번호, key 개수를 저장합니다.
- 노드 하나는 page 하나에 대응하며, 노드 간 연결은 page 번호로 표현합니다. page 0은 노드가 될 수 없으므로 page 번호 0을 "없음"으로 사용합니다.

### 노드 page 레이아웃
```
[0]     uint8   is_leaf
[1]     uint8   padding
[2..3]  uint16  key 개수
[4..7]  uint32  다음 leaf의 page 번호 (leaf 전용)
[8..]   int64   keys[n]
        leaf:     int64  values[n]
        internal: uint32 children[n + 1]
```
노드당 key 개수의 상한 `MAX_KEYS`(현재 255)는 컴파일 시점에 page 용량을 넘지 않는지 `static_assert`로 검사합니다.

### B+tree
- 실제 데이터는 leaf에만 저장하며, leaf끼리는 연결 리스트로 이어져 범위 검색 시 옆으로 순회합니다.
- internal 노드의 separator `keys[i]`에 대해, 그보다 작은 key는 `children[i]`, 크거나 같은 key는 `children[i+1]`에 위치합니다.
- 삽입은 root에서 한 번만 내려가며, 중복 key는 leaf에서 판별합니다. 넘친 노드는 split되어 separator를 부모로 전달하고, root가 split될 때만 트리 높이가 증가합니다.

### Buffer pool
- **LRU**: `std::list`(사용 순서)와 `std::unordered_map`(page 번호 → list 위치)으로 구현했습니다. 이미 있는 page는 `splice`로 맨 앞으로 옮겨 할당 없이 O(1)에 갱신합니다.
- **write-back**: `write`는 pool 안의 page만 갱신하고 dirty로 표시합니다. 디스크 쓰기는 해당 page가 내보내지거나 `flush()`가 호출될 때만 일어나므로, 같은 page를 여러 번 고쳐도 디스크에는 한 번만 기록됩니다.
- **쓰기 miss는 디스크를 읽지 않습니다.** `write`는 page 전체를 덮어쓰므로 원래 내용을 읽어올 필요가 없습니다. split으로 새로 할당한 page는 첫 접근이 항상 쓰기이므로 이 차이가 특히 큽니다.
- **flush**: dirty page를 모두 기록한 뒤 `fsync`를 한 번 호출합니다. 소멸자도 `flush()`를 시도하지만, 소멸자에서는 예외를 던질 수 없으므로 실패를 무시합니다. 오류를 확인하려면 `flush()`를 명시적으로 호출해야 합니다.
- **예외 처리**: miss 시 디스크 읽기를 pool 상태를 바꾸기 전에 수행하므로, 읽기가 실패해도 pool 상태는 그대로입니다. 내보낼 page의 디스크 쓰기가 실패하면 pool의 내부 불변식은 유지되지만 그 page의 최신 내용은 잃을 수 있으므로, 이후에는 해당 pool을 사용하지 않는 것을 전제로 합니다. 이 문제는 M5의 WAL에서 다룹니다.
- 교체 정책이 방금 들인 page를 곧바로 내보내면 계약 위반으로 보고 예외를 던집니다.

## 검증

- **정확성**: 오름차순, 내림차순, 무작위, 중복 다수 입력에 대해 `std::map`과 `find`/`range`/`size` 결과를 대조합니다. 파일을 닫았다가 다시 연 뒤에도 같은 결과가 나오는지 확인합니다. 이 테스트는 트리 크기보다 훨씬 작은 buffer pool 위에서 실행되어 eviction과 write-back이 계속 일어나는 상태를 검증합니다.
- **불변식**: `check()`가 노드 형태, key 정렬, 노드별 key 개수 범위, separator에 의한 key 범위, leaf 깊이 일치, leaf 연결, 전체 key 개수를 검사합니다.
- **I/O 횟수**: 트리 높이를 h, 삽입 중 발생한 split 횟수를 s라 할 때, 삽입은 page 읽기 h회와 쓰기 2 + 2s회, 검색은 읽기 h회를 정확히 수행하는지 매 연산마다 확인합니다.
- **Buffer pool**: hit 시 디스크 접근이 없는지, dirty page만 내보낼 때 기록되는지, 두 번째 `flush()`가 아무것도 쓰지 않는지를 디스크 I/O 횟수로 확인합니다. 무작위 읽기/쓰기 2만 회를 `std::map`과 대조하고, 종료 후 pool 없이 파일을 직접 읽어 디스크 내용까지 확인합니다.

## 벤치마크

무작위 순서의 key 10만 개를 삽입한 뒤 무작위 key 10만 개를 검색했습니다(그중 절반은 존재하지 않는 key). `MAX_KEYS = 255`, 최종 트리 높이 3, 전체 536 page입니다.

| pool 크기 | 논리 접근 | 디스크 읽기 | 디스크 쓰기 | hit rate | 시간(s) |
|---:|---:|---:|---:|---:|---:|
| 없음 | 753,903 | 552,837 | 201,066 | - | 0.340 |
| 1 | 753,903 | 552,837 | 201,066 | 13.3% | 0.627 |
| 2 | 753,903 | 552,323 | 200,555 | 13.4% | 0.625 |
| 4 | 753,903 | 253,106 | 98,754 | 66.3% | 0.481 |
| 8 | 753,903 | 169,568 | 94,889 | 77.4% | 0.454 |
| 16 | 753,903 | 139,220 | 88,790 | 81.5% | 0.427 |
| 64 | 753,903 | 108,382 | 64,756 | 85.6% | 0.396 |
| 256 | 753,903 | 44,236 | 19,031 | 94.1% | 0.305 |
| 1024 | 753,903 | 0 | 536 | 99.9% | 0.209 |

- **논리 접근 수는 모든 경우에 같습니다.** 트리가 저장소에 요청하는 읽기/쓰기는 buffer pool의 유무와 무관하며, 벤치마크가 이를 매번 검사합니다.
- **pool 1~2**: hit는 "leaf를 읽은 직후 같은 leaf에 쓰는" 경우뿐이며, 이 page도 곧바로 내보내지며 기록되므로 디스크 접근이 줄지 않습니다. 관리 비용만 더해져 오히려 느려집니다.
- **pool 4**: 모든 연산이 거치는 메타 page와 root가 pool에 상주하면서 디스크 읽기가 약 54% 줄었습니다. 삽입마다 기록되던 메타 page가 pool 안에서 dirty 상태로 유지되어 디스크 쓰기도 약 51% 줄었습니다.
- **pool 16~256**: 나머지 접근은 무작위 key로 인해 약 530개의 leaf에 고르게 분산되므로, pool이 담을 수 있는 leaf의 비율에 따라 점진적으로 개선됩니다.
- **pool 1024**: 트리 전체가 pool에 들어가 eviction이 발생하지 않습니다. 모든 page가 쓰기로 처음 생성되고 쓰기 miss는 디스크를 읽지 않으므로 디스크 읽기는 0이며, 디스크 쓰기는 마지막 `flush()`에서 page마다 한 번씩입니다.
- 시간은 참고치입니다. `Pager`의 읽기도 대부분 OS page cache에서 처리되므로 실제 디스크 지연은 반영되지 않습니다.

### Buffer pool 크기와 CPU cache

pool 1024에서도 남은 시간의 대부분은 사용자 공간 CPU였고(`user` 0.187s, `sys` 0.024s), perf로 측정한 최대 비용은 `BufferPool::read`의 4KB page 복사(약 25%)였습니다. 원인을 확인하기 위해 cachegrind로 코어당 캐시 구성(L1d 48KB, L2 2MB, L3 24MB)을 시뮬레이션했습니다.

| pool 크기 | read 1회당 L1 miss | read 1회당 L2 miss | L3 miss | deserialize miss |
|---:|---:|---:|---:|---:|
| 64 | 11.3 | 0 | 0 | 0 |
| 256 | 18.5 | 0 | 0 | 0 |
| 512 | 23.2 | 0.34 | 0 | 0 |
| 1024 | 22.8 | 0.56 | 0 | 0 |

- cache miss는 page를 처음 건드리는 `read`에 집중되며, 직후 같은 버퍼를 읽는 `deserialize`에서는 발생하지 않습니다.
- L2 miss는 pool이 L2 크기(2MB)를 넘는 지점부터 나타나지만 모두 L3에서 처리되어, 메모리까지 가는 접근은 없습니다.
- 따라서 이 규모에서 pool 확대에 따른 hit당 CPU 비용 증가는 작으며, 디스크 접근 감소의 이득이 이를 크게 상회합니다. 작은 pool에서 L1 miss가 적게 측정되는 것은, miss 시 커널이 frame에 데이터를 채우면서 발생하는 cache 비용이 사용자 공간만 측정하는 cachegrind에 잡히지 않기 때문입니다.

## 빌드 및 실행

Linux(WSL2 Ubuntu 22.04), g++ 11, CMake 3.22 환경에서 개발했습니다.
기본 빌드(Debug)에는 AddressSanitizer와 UndefinedBehaviorSanitizer가 활성화되어 있으며, 벤치마크는 sanitizer가 없는 Release 빌드로 측정합니다.

```bash
# 테스트 (Debug)
cmake -B build && cmake --build build
./build/pager_test
./build/lru_test
./build/buffer_pool_test
./build/bptree_test
./build/io_count_test

# 벤치마크 (Release)
cmake -B build-release -DCMAKE_BUILD_TYPE=Release && cmake --build build-release
./build-release/bench_pool
```

## License

MIT