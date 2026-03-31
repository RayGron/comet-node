function buildSkillSearchHaystack(item) {
  return [
    item?.id,
    item?.name,
    item?.description,
    item?.content,
    item?.plane_count,
    ...(Array.isArray(item?.plane_names) ? item.plane_names : []),
  ]
    .join(" ")
    .toLowerCase();
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
    return String(left?.name || "").localeCompare(String(right?.name || ""));
  });
  return nextItems;
}

export function filterSkillsFactoryItems(items, search) {
  const needle = String(search || "").trim().toLowerCase();
  return (Array.isArray(items) ? items : []).filter((item) => {
    if (!needle) {
      return true;
    }
    return buildSkillSearchHaystack(item).includes(needle);
  });
}

export function filterPlaneSelectableSkills(items, search) {
  return filterSkillsFactoryItems(items, search);
}
