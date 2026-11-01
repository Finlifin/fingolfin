"""Check saved registry ancestry, including sharing and isolation across bindings."""


def verify_tree(data):
    rows = data["bindings"]
    if (
        data["history_dropped"]
        or data["tree_checks"] != len(rows)
        or data["tree_invalidations"] != 3 * len(rows)
    ):
        raise RuntimeError("missing live-tree or teardown checks")
    if data["property_checks"] != len(rows) or data["stale_properties"] != len(rows):
        raise RuntimeError("missing live/stale property checks")
    identities = {}
    keys = {}

    def key(k):
        return tuple(k[f] for f in ("run", "launch", "slot", "generation"))

    def associate(identity, k):
        if identities.setdefault(identity, k) != k or keys.setdefault(k, identity) != identity:
            raise RuntimeError("tree identity alias or duplicate object")

    for b in rows:
        leaf = key(b)
        parents = [key(k) for k in b["ancestors"]]
        if len(parents) != 3 or len(set([leaf, *parents])) != 4:
            raise RuntimeError("invalid ancestry depth or cycle")
        if any(k[:2] != leaf[:2] or k[3] < 1 for k in parents):
            raise RuntimeError("parent from another run/launch or zero generation")
        prefix = leaf[:2]
        associate((*prefix, "launch"), parents[2])
        associate((*prefix, "device", b["device"]), parents[1])
        associate((*prefix, "core", b["device"], b["x"], b["y"]), parents[0])
        kind = "cb" if b["hook"] <= 6 else "noc" if b["hook"] <= 20 else "compute"
        associate((*prefix, kind, b["device"], b["x"], b["y"], b["local"]), leaf)
