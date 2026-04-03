export const NO_GROUP_TREE_PATH = "__no_group__";

function normalizeGroupPath(value) {
  if (String(value || "") === NO_GROUP_TREE_PATH) {
    return NO_GROUP_TREE_PATH;
  }
  return String(value || "")
    .split(/[\\/]/)
    .map((item) => item.trim())
    .filter(Boolean)
    .join("/");
}

function splitGroupPath(value) {
  const normalized = normalizeGroupPath(value);
  return normalized && normalized !== NO_GROUP_TREE_PATH ? normalized.split("/") : [];
}

export function formatSkillGroupPath(groupPath) {
  const normalized = normalizeGroupPath(groupPath);
  return normalized && normalized !== NO_GROUP_TREE_PATH ? normalized : "No group";
}

function buildSkillSearchHaystack(item) {
  return [
    item?.id,
    item?.name,
    normalizeGroupPath(item?.group_path),
    item?.description,
    item?.content,
    item?.internal ? "internal support layer" : "user facing",
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
  if (normalizedGroup === NO_GROUP_TREE_PATH) {
    return !itemGroup;
  }
  return itemGroup === normalizedGroup || itemGroup.startsWith(`${normalizedGroup}/`);
}

function sortGroupTree(node) {
  node.children.sort((left, right) => left.label.localeCompare(right.label));
  node.children.forEach(sortGroupTree);
  return node;
}

function ensureGroupNode(root, pathMap, groupPath) {
  const segments = splitGroupPath(groupPath);
  let currentNode = root;
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
    currentNode = child;
  }
  return currentNode;
}

export function buildSkillsFactoryGroupTree(items, explicitGroups = []) {
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
  const nextGroups = Array.isArray(explicitGroups) ? explicitGroups : [];

  for (const group of nextGroups) {
    const groupPath =
      typeof group === "string" ? normalizeGroupPath(group) : normalizeGroupPath(group?.path);
    if (!groupPath || groupPath === NO_GROUP_TREE_PATH) {
      continue;
    }
    ensureGroupNode(root, pathMap, groupPath);
  }

  let noGroupNode = null;
  for (const item of nextItems) {
    const segments = splitGroupPath(item?.group_path);
    root.total_skill_count += 1;
    if (segments.length === 0) {
      if (!noGroupNode) {
        noGroupNode = {
          path: NO_GROUP_TREE_PATH,
          label: "No group",
          depth: 1,
          direct_skill_count: 0,
          total_skill_count: 0,
          children: [],
        };
        root.children.push(noGroupNode);
      }
      noGroupNode.direct_skill_count += 1;
      noGroupNode.total_skill_count += 1;
      continue;
    }
    let currentNode = root;
    let currentPath = "";
    for (const segment of segments) {
      currentPath = currentPath ? `${currentPath}/${segment}` : segment;
      const child = pathMap.get(currentPath) || ensureGroupNode(root, pathMap, currentPath);
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

export function isBuiltinSkillsFactoryGroupPath(path) {
  return normalizeGroupPath(path) === NO_GROUP_TREE_PATH;
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
