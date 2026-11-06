# minidb

C++17로 처음부터 구현하는 디스크 기반 storage engine입니다.
고정 크기 page 단위 파일 I/O 위에 B+tree index를 올리고, 이후 buffer pool과 WAL 기반 crash recovery까지 단계적으로 확장하는 것을 목표로 합니다.

## 현재 구현 범위

| 단계 | 내용 | 상태 |
|---|---|---|
| M1 | Pager: 4096B page 단위 파일 I/O (`pread`/`pwrite`/`fsync`) | 완료 |
| M2 | 메모리 B+tree: 삽입, 검색, 범위 검색, 불변식 검사 | 완료 |
| M3 | 디스크 B+tree: 노드 직렬화, 메타 page, 재오픈 시 영속성 | 완료 |
| M4 | Buffer pool (LRU, write-back) | 진행 예정 |
| M5 | WAL과 crash recovery | 계획 |

## 구조

```
src/
├── pager.h / pager.cpp    page 번호 ↔ 파일 오프셋 변환, 읽기/쓰기, I/O 횟수 집계
├── node.h  / node.cpp     B+tree 노드 ↔ 4096B page 직렬화
└── bptree.h / bptree.cpp  page 기반 B+tree
tests/
├── pager_test.cpp         page 읽기/쓰기, 재오픈, 범위 밖 접근
├── bptree_test.cpp        std::map 대조 테스트, 재오픈 후 일관성
└── io_count_test.cpp      연산별 디스크 I/O 횟수를 이론값과 비교
```

## 설계

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
노드당 key 개수의 상한 `MAX_KEYS`는 컴파일 시점에 page 용량을 넘지 않는지 `static_assert`로 검사합니다.

### B+tree
- 실제 데이터는 leaf에만 저장하며, leaf끼리는 연결 리스트로 이어져 범위 검색 시 옆으로 순회합니다.
- internal 노드의 separator `keys[i]`에 대해, 그보다 작은 key는 `children[i]`, 크거나 같은 key는 `children[i+1]`에 위치합니다.
- 삽입은 root에서 한 번만 내려가며, 중복 key는 leaf에서 판별합니다. 넘친 노드는 split되어 separator를 부모로 전달하고, root가 split될 때만 트리 높이가 증가합니다.

## 검증

- **정확성**: 오름차순, 내림차순, 무작위, 중복 다수 입력에 대해 `std::map`과 `find`/`range`/`size` 결과를 대조합니다. 파일을 닫았다가 다시 연 뒤에도 같은 결과가 나오는지 확인합니다.
- **불변식**: `check()`가 노드 형태, key 정렬, 노드별 key 개수 범위, separator에 의한 key 범위, leaf 깊이 일치, leaf 연결, 전체 key 개수를 검사합니다.
- **I/O 횟수**: 트리 높이를 h, 삽입 중 발생한 split 횟수를 s라 할 때, 삽입은 page 읽기 h회와 쓰기 2 + 2s회, 검색은 읽기 h회를 정확히 수행하는지 매 연산마다 확인합니다.

## 빌드 및 실행

Linux(WSL2 Ubuntu 22.04), g++ 11, CMake 3.22 환경에서 개발했습니다.
디버그 빌드에는 AddressSanitizer와 UndefinedBehaviorSanitizer가 기본으로 활성화되어 있습니다.

```bash
cmake -B build && cmake --build build
./build/pager_test
./build/bptree_test
./build/io_count_test
```

## License

MIT