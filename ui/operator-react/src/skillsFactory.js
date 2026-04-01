function normalizeGroupPath(value) {
  return String(value || "")
    .split(/[\\/]/)
    .map((item) => item.trim())
    .filter(Boolean)
    .join("/");
}

function splitGroupPath(value) {
  const normalized = normalizeGroupPath(value);
  return normalized ? normalized.split("/") : [];
}

export function formatSkillGroupPath(groupPath) {
  const normalized = normalizeGroupPath(groupPath);
  return normalized || "Ungrouped";
}

function buildSkillSearchHaystack(item) {
  return [
    item?.id,
    item?.name,
    normalizeGroupPath(item?.group_path),
    item?.description,
    item?.content,
    item?.plane_count,
    ...(Array.isArray(item?.plane_names) ? item.plane_names : []),
  ]
    .join(" ")
    .toLowerCase();
}

function matchesSelectedGroup(item, selectedGroupPath) {
  const normalizedGroup = normalizeGroupPath(selectedGroupPath);
  if (!normalizedGroup) {
    return true;
  }
  const itemGroup = normalizeGroupPath(item?.group_path);
  return itemGroup === normalizedGroup || itemGroup.startsWith(`${normalizedGroup}/`);
}

function sortGroupTree(node) {
  node.children.sort((left, right) => left.label.localeCompare(right.label));
  node.children.forEach(sortGroupTree);
  return node;
}

export function buildSkillsFactoryGroupTree(items) {
  const root = {
    path: "",
    label: "All skills",
    depth: 0,
    direct_skill_count: 0,
    total_skill_count: 0,
    children: [],
  };
  const pathMap = new Map([["", root]]);
  const nextItems = Array.isArray(items) ? items : [];

  for (const item of nextItems) {
    const segments = splitGroupPath(item?.group_path);
    let currentNode = root;
    currentNode.total_skill_count += 1;
    if (segments.length === 0) {
      currentNode.direct_skill_count += 1;
      continue;
    }
    let currentPath = "";
    for (const segment of segments) {
      currentPath = currentPath ? `${currentPath}/${segment}` : segment;
      let child = pathMap.get(currentPath);
      if (!child) {
        child = {
          path: currentPath,
          label: segment,
          depth: splitGroupPath(currentPath).length,
          direct_skill_count: 0,
          total_skill_count: 0,
          children: [],
        };
        pathMap.set(currentPath, child);
        currentNode.children.push(child);
      }
      child.total_skill_count += 1;
      currentNode = child;
    }
    currentNode.direct_skill_count += 1;
  }

  return sortGroupTree(root);
}

export function collectSkillsFactoryTreePaths(node) {
  if (!node || typeof node !== "object") {
    return [];
  }
  return [
    node.path || "",
    ...(Array.isArray(node.children)
      ? node.children.flatMap((child) => collectSkillsFactoryTreePaths(child))
      : []),
  ];
}

export function collectGroupSkillIds(items, selectedGroupPath) {
  return (Array.isArray(items) ? items : [])
    .filter((item) => matchesSelectedGroup(item, selectedGroupPath))
    .map((item) => item.id)
    .filter(Boolean);
}

export function sortSkillsFactoryItems(items, sortKey) {
  const nextItems = [...(Array.isArray(items) ? items : [])];
  nextItems.sort((left, right) => {
    if (sortKey === "plane_count") {
      const diff = Number(right?.plane_count || 0) - Number(left?.plane_count || 0);
      if (diff !== 0) {
        return diff;
      }
    }
    const groupDiff = normalizeGroupPath(left?.group_path).localeCompare(
      normalizeGroupPath(right?.group_path),
    );
    if (groupDiff !== 0) {
      return groupDiff;
    }
    return String(left?.name || "").localeCompare(String(right?.name || ""));
  });
  return nextItems;
}

export function filterSkillsFactoryItems(items, search, selectedGroupPath = "") {
  const needle = String(search || "").trim().toLowerCase();
  return (Array.isArray(items) ? items : []).filter((item) => {
    if (!matchesSelectedGroup(item, selectedGroupPath)) {
      return false;
    }
    if (!needle) {
      return true;
    }
    return buildSkillSearchHaystack(item).includes(needle);
  });
}

export function filterPlaneSelectableSkills(items, search, selectedGroupPath = "") {
  return filterSkillsFactoryItems(items, search, selectedGroupPath);
}
