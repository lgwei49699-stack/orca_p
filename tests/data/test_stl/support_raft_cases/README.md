# Support and raft STL fixtures

These ASCII STL files cover common slicing UI/test scenarios with recognizable, printer-friendly shapes:

- `normal_house.stl`: a small house with a broad base, sloped roof, chimney, doors, and window details; it should not need support or raft.
- `support_required_balcony.stl`: a pavilion with columns, roof, and a side balcony/shelf that has a clear unsupported underside; support behavior is easy to inspect, raft is not required.
- `raft_recommended_needle_tower.stl`: a tall needle tower with a tiny footprint and vertical fins; support is not required, but raft or another bed-adhesion helper is recommended.
- `support_and_raft_signpost.stl`: a tall narrow signpost with a cantilevered sign and roof; both support and raft/bed-adhesion behavior can be tested.

Regenerate the files from this directory with:

```sh
python3 generate_support_raft_cases.py
```
