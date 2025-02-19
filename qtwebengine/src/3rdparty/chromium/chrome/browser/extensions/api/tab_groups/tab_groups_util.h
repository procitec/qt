// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_TAB_GROUPS_TAB_GROUPS_UTIL_H_
#define CHROME_BROWSER_EXTENSIONS_API_TAB_GROUPS_TAB_GROUPS_UTIL_H_

#include <optional>
#include <string>

#include "chrome/common/extensions/api/tab_groups.h"
#include "components/tab_groups/tab_group_color.h"

class Browser;

namespace content {
class BrowserContext;
}

class TabStripModel;
namespace tab_groups {
class TabGroupId;
class TabGroupVisualData;
}  // namespace tab_groups

namespace extensions {

// Provides various utility functions that help manipulate tab groups.
namespace tab_groups_util {

// Gets the extensions-specific Group ID.
int GetGroupId(const tab_groups::TabGroupId& id);

// Gets the window ID that the group belongs to.
int GetWindowIdOfGroup(const tab_groups::TabGroupId& id);

// Creates a TabGroup object
// (see chrome/common/extensions/api/tab_groups.json) with information about
// the state of a tab group for the given group |id|. Most group metadata is
// derived from the |visual_data|, which specifies group color, title, etc.
api::tab_groups::TabGroup CreateTabGroupObject(
    const tab_groups::TabGroupId& id,
    const tab_groups::TabGroupVisualData& visual_data);
std::optional<api::tab_groups::TabGroup> CreateTabGroupObject(
    const tab_groups::TabGroupId& id);

// Gets the metadata for the group with ID |group_id|. Sets the |error| if not
// found. |browser|, |id|, or |visual_data| may be nullptr and will not be set
// within the function if so.
bool GetGroupById(int group_id,
                  content::BrowserContext* browser_context,
                  bool include_incognito,
                  Browser** browser,
                  tab_groups::TabGroupId* id,
                  const tab_groups::TabGroupVisualData** visual_data,
                  std::string* error);
bool GetGroupById(int group_id,
                  content::BrowserContext* browser_context,
                  bool include_incognito,
                  tab_groups::TabGroupId* id,
                  std::string* error);

// Conversions between the api::tab_groups::Color enum and the TabGroupColorId
// enum.
api::tab_groups::Color ColorIdToColor(
    const tab_groups::TabGroupColorId& color_id);
tab_groups::TabGroupColorId ColorToColorId(api::tab_groups::Color color);

bool IsGroupSaved(tab_groups::TabGroupId tab_group_id,
                  TabStripModel* tab_strip_model);

}  // namespace tab_groups_util
}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_TAB_GROUPS_TAB_GROUPS_UTIL_H_
