/** @file
  角色数据表 (从 ASM 数据表提取, 当前为占位)

  各角色的动画数据将从 Ref/asm/ 中逐角色提取。
  提取方法: 搜索各角色 ID 相关的数据表引用。

  数组按角色 ID 索引 (0-8), ID 5 不存在。
  PaletteIndex 暂时使用 ID 序号, 后续从
  sub_E783_select_colors_for_fighter 提取真实调色板映射。
**/

#include "fighter.h"

/* 角色数据表: 索引 = 角色 ID (0-8), ID 5 不存在 */
FIGHTER_DATA gFighterData[FIGHTER_ID_COUNT] = {
  { 0, { NULL } },  /* 0: Chun-Li  */
  { 1, { NULL } },  /* 1: Ryu/Ken  */
  { 2, { NULL } },  /* 2: Guile    */
  { 3, { NULL } },  /* 3: Blanka   */
  { 4, { NULL } },  /* 4: Dhalsim  */
  { 0, { NULL } },  /* 5: (不存在) */
  { 5, { NULL } },  /* 6: Balrog   */
  { 6, { NULL } },  /* 7: Sagat    */
  { 7, { NULL } },  /* 8: Vega     */
};
