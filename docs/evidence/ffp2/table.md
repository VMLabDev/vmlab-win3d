| Case | w11-nvidia2 before (HW, SW, MHW, MSW) | w11-nvidia2 after (HW, SW, MHW, MSW) | w11-amd1 before (HW, SW, MHW, MSW) | w11-amd1 after (HW, SW, MHW, MSW) | w10-nvidia2 before (HW, SW, MHW, MSW) | w10-nvidia2 after (HW, SW, MHW, MSW) | REF: SWVP = HWVP |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `vb_0weights` | pass | pass | pass | pass | pass | pass | yes |
| `vb_1weight` | pass | pass | pass | pass | pass | pass | yes |
| `vb_2weights` | pass | pass | pass | pass | pass | pass | yes |
| `vb_3weights` | pass | pass | pass | pass | pass | pass | yes |
| `vb_1of3weights` | pass | pass | pass | pass | pass | pass | yes |
| `vb_2weights_lit` | pass | pass | pass | pass | pass | pass | yes |
| `vb_3weights_lit_persp` | pass | pass | pass | pass | pass | pass | yes |
| `vb_2weights_texgen` | pass | pass | pass | pass | pass | pass | yes |
| `vb_3weights_normal` | pass | pass | pass | pass | pass | pass | yes |
| `vb_indexed_0w` | pass | pass | pass | pass | pass | pass | yes |
| `vb_indexed_1w` | pass | pass | pass | pass | pass | pass | yes |
| `vb_indexed_2w` | pass | pass | pass | pass | pass | pass | yes |
| `vb_indexed_3w` | pass | pass | pass | pass | pass | pass | yes |
| `vb_indexed_color` | fail d=134 k=64, pass, fail d=134 k=64, pass | fail d=134 k=64, pass, fail d=134 k=64, pass | fail d=134 k=64, pass, fail d=134 k=64, pass | fail d=134 k=64, pass, fail d=134 k=64, pass | fail d=134 k=64, pass, fail d=134 k=64, pass | fail d=134 k=64, pass, fail d=134 k=64, pass | differs |
| `vb_indexed_lit` | pass | pass | pass | pass | pass | pass | yes |
| `vb_indexed_texgen` | pass | pass | pass | pass | pass | pass | yes |
| `vb_tween_color` | pass | pass | pass | pass | pass | pass | yes |
| `vb_tween_lit` | pass | pass | pass | pass | pass | pass | yes |
| `bm_const_xyz` | pass | pass | pass | pass | pass | pass | yes |
| `bm_const_rhw` | pass | pass | pass | pass | pass | pass | yes |
| `bm_varying_xyz` | pass | pass | pass | pass | pass | pass | yes |
| `bm_varying_rhw` | pass | pass | pass | pass | pass | pass | yes |
| `bm_matrix` | pass | pass | pass | pass | pass | pass | yes |
| `bm_x8l8v8u8` | HR 0x8876086a, HR 0x8876086a, HR 0x8876086a, HR 0x8876086a | pass | HR 0x8876086a, HR 0x8876086a, HR 0x8876086a, HR 0x8876086a | pass | HR 0x8876086a, HR 0x8876086a, HR 0x8876086a, HR 0x8876086a | pass | yes |
| `bm_lum_x8l8v8u8` | HR 0x8876086a, HR 0x8876086a, HR 0x8876086a, HR 0x8876086a | pass | HR 0x8876086a, HR 0x8876086a, HR 0x8876086a, HR 0x8876086a | pass | HR 0x8876086a, HR 0x8876086a, HR 0x8876086a, HR 0x8876086a | pass | yes |
| `bm_lum_l6v5u5` | HR 0x8876086a, HR 0x8876086a, HR 0x8876086a, HR 0x8876086a | pass | HR 0x8876086a, HR 0x8876086a, HR 0x8876086a, HR 0x8876086a | pass | HR 0x8876086a, HR 0x8876086a, HR 0x8876086a, HR 0x8876086a | pass | yes |
| `bm_lum_rhw` | HR 0x8876086a, HR 0x8876086a, HR 0x8876086a, HR 0x8876086a | pass | HR 0x8876086a, HR 0x8876086a, HR 0x8876086a, HR 0x8876086a | pass | HR 0x8876086a, HR 0x8876086a, HR 0x8876086a, HR 0x8876086a | pass | yes |
| `bm_stage1` | pass | pass | pass | pass | pass | pass | yes |
| `bm_stage1_lum` | HR 0x8876086a, HR 0x8876086a, HR 0x8876086a, HR 0x8876086a | pass | HR 0x8876086a, HR 0x8876086a, HR 0x8876086a, HR 0x8876086a | pass | HR 0x8876086a, HR 0x8876086a, HR 0x8876086a, HR 0x8876086a | pass | yes |
| `bm_texgen` | pass | pass | pass | pass | pass | pass | yes |
| `cm_explicit` | pass | pass | pass | pass | pass | pass | yes |
| `cm_explicit_rhw` | pass | pass | pass | pass | pass | pass | yes |
| `cm_explicit_count3` | pass | pass | pass | pass | pass | pass | yes |
| `cm_reflect` | pass | pass | pass | pass | pass | pass | yes |
| `cm_reflect_nolocal` | pass | pass | pass | pass | pass | pass | yes |
| `cm_reflect_count3` | pass | pass | pass | pass | pass | pass | yes |
| `cm_reflect_persp` | pass | pass | pass | pass | pass | pass | yes |
| `cm_normal` | pass | pass | pass | pass | pass | pass, pass, pass, HR 0x8007000e | yes |
| `cm_normal_count3` | pass | pass | pass | pass | pass | pass | yes |
| `cm_position` | pass | pass | pass | pass | pass | pass | yes |
| `vol_explicit` | pass | pass | pass | pass | pass | pass | yes |
| `vol_campos` | pass | pass | pass | pass | pass | pass | yes |
| `mx_hw_sw` | pass | pass | pass | pass | pass | pass | yes |
| `mx_sw_hw` | pass | pass | pass | pass | pass | pass | yes |
| `mx_all_sw` | pass | pass | pass | pass | pass | pass | yes |
| `mx_vb_hw_sw` | pass | pass | pass | pass | pass | pass | yes |
| `mx_features_hw_sw` | pass | pass | pass | pass | pass | pass | yes |
| `mx_features_sw_hw` | pass | pass | pass | pass | pass | pass | yes |
| `mx_indexed_high` | pass | pass | pass | pass | pass | pass | yes |

| `d3d9_ffp2` run | w11-nvidia2 before | w11-nvidia2 after | w11-amd1 before | w11-amd1 after | w10-nvidia2 before | w10-nvidia2 after |
| --- | --- | --- | --- | --- | --- | --- |
| x64-hw against x64-ref | 43 of 49 | 48 of 49 | 43 of 49 | 48 of 49 | 43 of 49 | 48 of 49 |
| x86-hw against x86-ref | 43 of 49 | 47 of 49 | 43 of 49 | 48 of 49 | 43 of 49 | 48 of 49 |
| x64-swvp against x64-refswvp | 44 of 49 | 49 of 49 | 44 of 49 | 49 of 49 | 44 of 49 | 49 of 49 |
| x64-mixhw against x64-ref | 43 of 49 | 48 of 49 | 43 of 49 | 48 of 49 | 43 of 49 | 48 of 49 |
| x64-mixsw against x64-refswvp | 44 of 49 | 49 of 49 | 44 of 49 | 49 of 49 | 44 of 49 | 48 of 49 |
| x64-hw against x64-refswvp | 44 of 49 | 49 of 49 | 44 of 49 | 49 of 49 | 44 of 49 | 49 of 49 |

| `d3d9_ffp` run | w11-nvidia2 before | w11-nvidia2 after | w11-amd1 before | w11-amd1 after | w10-nvidia2 before | w10-nvidia2 after |
| --- | --- | --- | --- | --- | --- | --- |
| x64-hw against x64-ref | 166 of 168 | 166 of 168 | - | 166 of 168 | 166 of 168 | 166 of 168 |
| x86-hw against x86-ref | - | 166 of 168 | - | 166 of 168 | 166 of 168 | 166 of 168 |
| x64-swvp against x64-refswvp | - | 168 of 168 | - | 168 of 168 | 168 of 168 | 168 of 168 |
| x64-mixhw against x64-ref | 166 of 168 | 166 of 168 | 166 of 168 | 166 of 168 | 166 of 168 | 166 of 168 |
| x64-mixsw against x64-refswvp | 168 of 168 | 168 of 168 | 168 of 168 | 168 of 168 | 168 of 168 | 168 of 168 |
| x64-hw against x64-refswvp | 168 of 168 | 168 of 168 | - | 168 of 168 | 168 of 168 | 168 of 168 |

