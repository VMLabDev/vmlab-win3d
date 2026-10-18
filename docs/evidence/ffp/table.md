| Case | nvidia2 before HWVP | nvidia2 before SWVP | nvidia2 after HWVP | nvidia2 after SWVP | amd1 before HWVP | amd1 before SWVP | amd1 after HWVP | amd1 after SWVP | REF: SWVP = HWVP |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `b_sel_tex-rhw` | pass | pass | pass | pass | pass | pass | pass | pass | yes |
| `b_sel_tex-xyz` | pass | pass | pass | pass | pass | pass | pass | pass | yes |
| `b_sel_diff-rhw` | pass | fail d=204 k=64 | pass | pass | pass | fail d=204 k=64 | pass | pass | yes |
| `b_sel_diff-xyz` | pass | fail d=204 k=64 | pass | pass | pass | fail d=204 k=64 | pass | pass | yes |
| `b_sel_tf-rhw` | pass | pass | pass | pass | pass | pass | pass | pass | yes |
| `b_sel_tf-xyz` | pass | pass | pass | pass | pass | pass | pass | pass | yes |
| `b_sel_spec-rhw` | pass | fail d=191 k=64 | pass | pass | pass | fail d=223 k=64 | pass | pass | yes |
| `b_sel_spec-xyz` | pass | fail d=191 k=64 | pass | pass | pass | fail d=223 k=64 | pass | pass | yes |
| `b_mod-rhw` | pass | fail d=154 k=64 | pass | pass | pass | fail d=242 k=64 | pass | pass | yes |
| `b_mod-xyz` | pass | fail d=154 k=64 | pass | pass | pass | fail d=242 k=64 | pass | pass | yes |
| `b_mod2x-rhw` | pass | fail d=255 k=64 | pass | pass | pass | fail d=229 k=64 | pass | pass | yes |
| `b_mod2x-xyz` | pass | fail d=255 k=64 | pass | pass | pass | fail d=229 k=64 | pass | pass | yes |
| `b_mod4x-rhw` | pass | fail d=255 k=64 | pass | pass | pass | fail d=204 k=64 | pass | pass | yes |
| `b_mod4x-xyz` | pass | fail d=255 k=64 | pass | pass | pass | fail d=204 k=64 | pass | pass | yes |
| `b_add-rhw` | pass | fail d=102 k=64 | pass | pass | pass | fail d=140 k=64 | pass | pass | yes |
| `b_add-xyz` | pass | fail d=102 k=64 | pass | pass | pass | fail d=140 k=64 | pass | pass | yes |
| `b_addsigned-rhw` | pass | fail d=191 k=64 | pass | pass | pass | fail d=255 k=64 | pass | pass | yes |
| `b_addsigned-xyz` | pass | fail d=191 k=64 | pass | pass | pass | fail d=255 k=64 | pass | pass | yes |
| `b_addsigned2x-rhw` | pass | fail d=204 k=64 | pass | pass | pass | fail d=255 k=64 | pass | pass | yes |
| `b_addsigned2x-xyz` | pass | fail d=204 k=64 | pass | pass | pass | fail d=255 k=64 | pass | pass | yes |
| `b_subtract-rhw` | pass | fail d=192 k=64 | pass | pass | pass | fail d=255 k=64 | pass | pass | yes |
| `b_subtract-xyz` | pass | fail d=192 k=64 | pass | pass | pass | fail d=255 k=64 | pass | pass | yes |
| `b_addsmooth-rhw` | pass | fail d=51 k=64 | pass | pass | pass | fail d=153 k=64 | pass | pass | yes |
| `b_addsmooth-xyz` | pass | fail d=51 k=64 | pass | pass | pass | fail d=153 k=64 | pass | pass | yes |
| `b_blendtexalpha-rhw` | pass | fail d=63 k=64 | pass | pass | pass | fail d=148 k=64 | pass | pass | yes |
| `b_blendtexalpha-xyz` | pass | fail d=63 k=64 | pass | pass | pass | fail d=148 k=64 | pass | pass | yes |
| `b_blenddiffalpha-rhw` | pass | fail d=51 k=64 | pass | pass | pass | fail d=140 k=64 | pass | pass | yes |
| `b_blenddiffalpha-xyz` | pass | fail d=51 k=64 | pass | pass | pass | fail d=140 k=64 | pass | pass | yes |
| `b_blendfactoralpha-rhw` | pass | fail d=102 k=64 | pass | pass | pass | fail d=197 k=64 | pass | pass | yes |
| `b_blendfactoralpha-xyz` | pass | fail d=102 k=64 | pass | pass | pass | fail d=197 k=64 | pass | pass | yes |
| `b_blendcurrentalpha-rhw` | pass | fail d=102 k=64 | pass | pass | pass | fail d=102 k=64 | pass | pass | yes |
| `b_blendcurrentalpha-xyz` | pass | fail d=102 k=64 | pass | pass | pass | fail d=102 k=64 | pass | pass | yes |
| `b_blendtexalphapm-rhw` | pass | fail d=63 k=64 | pass | pass | pass | fail d=128 k=64 | pass | pass | yes |
| `b_blendtexalphapm-xyz` | pass | fail d=63 k=64 | pass | pass | pass | fail d=128 k=64 | pass | pass | yes |
| `b_modalpha_addcolor-rhw` | pass | fail d=70 k=64 | pass | pass | pass | fail d=156 k=64 | pass | pass | yes |
| `b_modalpha_addcolor-xyz` | pass | fail d=70 k=64 | pass | pass | pass | fail d=156 k=64 | pass | pass | yes |
| `b_modcolor_addalpha-rhw` | pass | fail d=79 k=64 | pass | pass | pass | fail d=66 k=64 | pass | pass | yes |
| `b_modcolor_addalpha-xyz` | pass | fail d=79 k=64 | pass | pass | pass | fail d=66 k=64 | pass | pass | yes |
| `b_dot3-rhw` | pass | pass | pass | pass | pass | pass | pass | pass | yes |
| `b_dot3-xyz` | pass | pass | pass | pass | pass | pass | pass | pass | yes |
| `b_dot3_alpha-rhw` | pass | fail d=141 k=64 | pass | pass | pass | fail d=114 k=64 | pass | pass | yes |
| `b_dot3_alpha-xyz` | pass | fail d=141 k=64 | pass | pass | pass | fail d=114 k=64 | pass | pass | yes |
| `b_lerp-rhw` | pass | fail d=153 k=64 | pass | pass | pass | fail d=194 k=64 | pass | pass | yes |
| `b_lerp-xyz` | pass | fail d=153 k=64 | pass | pass | pass | fail d=194 k=64 | pass | pass | yes |
| `b_multiplyadd-rhw` | pass | fail d=186 k=64 | pass | pass | pass | fail d=146 k=64 | pass | pass | yes |
| `b_multiplyadd-xyz` | pass | fail d=186 k=64 | pass | pass | pass | fail d=146 k=64 | pass | pass | yes |
| `b_complement-rhw` | pass | fail d=70 k=64 | pass | pass | pass | fail d=217 k=64 | pass | pass | yes |
| `b_complement-xyz` | pass | fail d=70 k=64 | pass | pass | pass | fail d=217 k=64 | pass | pass | yes |
| `b_alpharep-rhw` | pass | fail d=77 k=64 | pass | pass | pass | fail d=217 k=64 | pass | pass | yes |
| `b_alpharep-xyz` | pass | fail d=77 k=64 | pass | pass | pass | fail d=217 k=64 | pass | pass | yes |
| `b_comp_alpharep-rhw` | pass | fail d=96 k=64 | pass | pass | pass | fail d=144 k=64 | pass | pass | yes |
| `b_comp_alpharep-xyz` | pass | fail d=96 k=64 | pass | pass | pass | fail d=144 k=64 | pass | pass | yes |
| `b_2stage_add-rhw` | pass | fail d=154 k=64 | pass | pass | pass | fail d=58 k=64 | pass | pass | yes |
| `b_2stage_add-xyz` | pass | fail d=154 k=64 | pass | pass | pass | fail d=58 k=64 | pass | pass | yes |
| `b_2stage_lightmap-rhw` | pass | pass | pass | pass | pass | pass | pass | pass | yes |
| `b_2stage_lightmap-xyz` | pass | pass | pass | pass | pass | pass | pass | pass | yes |
| `b_3stage-rhw` | pass | fail d=109 k=64 | pass | pass | pass | fail d=122 k=64 | pass | pass | yes |
| `b_3stage-xyz` | pass | fail d=109 k=64 | pass | pass | pass | fail d=122 k=64 | pass | pass | yes |
| `b_4stage-rhw` | pass | fail d=191 k=64 | pass | pass | pass | fail d=255 k=64 | pass | pass | yes |
| `b_4stage-xyz` | pass | fail d=191 k=64 | pass | pass | pass | fail d=255 k=64 | pass | pass | yes |
| `b_disable_stage1-rhw` | pass | fail d=154 k=64 | pass | pass | pass | fail d=242 k=64 | pass | pass | yes |
| `b_disable_stage1-xyz` | pass | fail d=154 k=64 | pass | pass | pass | fail d=242 k=64 | pass | pass | yes |
| `b_temp-rhw` | pass | fail d=70 k=64 | pass | pass | pass | fail d=146 k=64 | pass | pass | yes |
| `b_temp-xyz` | pass | fail d=70 k=64 | pass | pass | pass | fail d=146 k=64 | pass | pass | yes |
| `b_specularenable-rhw` | pass | fail d=186 k=64 | pass | pass | pass | fail d=149 k=64 | pass | pass | yes |
| `b_specularenable-xyz` | pass | fail d=186 k=64 | pass | pass | pass | fail d=149 k=64 | pass | pass | yes |
| `b_constant-rhw` | pass | pass | pass | pass | pass | pass | pass | pass | yes |
| `b_constant-xyz` | pass | pass | pass | pass | pass | pass | pass | pass | yes |
| `b_defaults-rhw` | pass | pass | pass | pass | pass | pass | pass | pass | yes |
| `b_defaults-xyz` | pass | pass | pass | pass | pass | pass | pass | pass | yes |
| `tg_identity` | pass | fail d=240 k=64 | pass | pass | pass | fail d=240 k=64 | pass | pass | yes |
| `tg_rhw_identity` | pass | fail d=240 k=64 | pass | pass | pass | fail d=240 k=64 | pass | pass | yes |
| `tg_index1` | pass | fail d=240 k=64 | pass | pass | pass | fail d=240 k=64 | pass | pass | yes |
| `tg_count2` | pass | fail d=212 k=64 | pass | pass | pass | fail d=211 k=64 | pass | pass | yes |
| `tg_count2_rhw` | pass | fail d=240 k=64 | pass | pass | pass | fail d=240 k=64 | pass | pass | yes |
| `tg_count3_projected` | pass | fail d=198 k=64 | pass | pass | pass | fail d=198 k=64 | pass | pass | yes |
| `tg_count4_projected` | pass | fail d=222 k=64 | pass | pass | pass | fail d=222 k=64 | pass | pass | yes |
| `tg_campos` | pass | fail d=240 k=64 | pass | pass | pass | fail d=240 k=64 | pass | pass | yes |
| `tg_campos_world` | pass | fail d=208 k=16 | pass | pass | pass | fail d=208 k=16 | pass | pass | yes |
| `tg_camnormal` | pass | fail d=195 k=64 | pass | pass | pass | fail d=195 k=64 | pass | pass | yes |
| `tg_camnormal_rot` | pass | pass | pass | pass | pass | pass | pass | pass | yes |
| `tg_camreflect` | pass | fail d=137 k=64 | pass | pass | pass | fail d=137 k=64 | pass | pass | yes |
| `tg_camreflect_nolocal` | pass | fail d=199 k=64 | pass | pass | pass | fail d=199 k=64 | pass | pass | yes |
| `tg_sphere` | pass | fail d=207 k=64 | pass | pass | pass | fail d=207 k=64 | pass | pass | yes |
| `tg_count3_normal_projected` | pass | pass | pass | pass | pass | pass | pass | pass | yes |
| `fv_linear` | fail d=39 k=64 | fail d=207 k=48 | pass | pass | fail d=39 k=64 | fail d=255 k=64 | pass | pass | yes |
| `fv_exp` | pass | fail d=196 k=64 | pass | pass | pass | fail d=216 k=64 | pass | pass | yes |
| `fv_exp2` | pass | fail d=194 k=64 | pass | pass | pass | fail d=239 k=64 | pass | pass | yes |
| `ft_linear` | pass | fail d=192 k=48 | pass | pass | pass | fail d=223 k=48 | pass | pass | yes |
| `ft_exp` | pass | fail d=169 k=64 | pass | pass | pass | fail d=197 k=64 | pass | pass | yes |
| `ft_exp2` | pass | fail d=190 k=64 | pass | pass | pass | fail d=220 k=64 | pass | pass | yes |
| `fv_linear_tex` | fail d=43 k=64 | fail d=231 k=48 | pass | pass | fail d=43 k=64 | fail d=255 k=64 | pass | pass | yes |
| `fv_range` | pass | pass | pass | pass | pass | fail d=255 k=64 | pass | pass | yes |
| `ft_linear_persp` | fail d=125 k=48 | fail d=192 k=56 | pass | pass | fail d=125 k=48 | fail d=223 k=56 | pass | pass | yes |
| `ft_exp_persp` | fail d=122 k=56 | fail d=164 k=56 | pass | pass | fail d=122 k=56 | fail d=175 k=56 | pass | pass | yes |
| `fv_linear_persp` | fail d=17 k=44 | fail d=207 k=56 | pass | pass | fail d=17 k=44 | fail d=192 k=56 | pass | pass | yes |
| `fv_exp2_persp` | pass | fail d=174 k=56 | pass | pass | pass | fail d=166 k=56 | pass | pass | yes |
| `fog_rhw_spec` | fail d=194 k=64 | fail d=194 k=64 | pass | pass | fail d=242 k=64 | fail d=242 k=64 | pass | pass | yes |
| `fog_rhw_table` | pass | pass | pass | pass | pass | pass | pass | pass | yes |
| `fog_rhw_nospec` | pass | pass | pass | pass | fail d=255 k=64 | fail d=255 k=64 | pass | pass | yes |
| `lt_dir` | pass | fail d=239 k=64 | pass | pass | pass | fail d=217 k=64 | pass | pass | yes |
| `lt_dir_spec` | pass | fail d=255 k=64 | pass | pass | pass | fail d=217 k=64 | pass | pass | yes |
| `lt_dir_spec_nolocal` | pass | fail d=255 k=64 | pass | pass | pass | fail d=217 k=64 | pass | pass | yes |
| `lt_point` | pass | fail d=199 k=64 | pass | pass | pass | fail d=225 k=64 | pass | pass | yes |
| `lt_point_spec` | pass | fail d=255 k=64 | pass | pass | pass | fail d=225 k=64 | pass | pass | yes |
| `lt_point_range` | pass | fail d=76 k=64 | pass | pass | pass | fail d=235 k=64 | pass | pass | yes |
| `lt_spot` | fail d=12 k=5 | fail d=193 k=64 | fail d=12 k=5 | pass | fail d=12 k=5 | fail d=235 k=64 | fail d=12 k=5 | pass | differs |
| `lt_spot_falloff` | fail d=9 k=2 | fail d=184 k=64 | fail d=9 k=2 | pass | fail d=9 k=2 | fail d=235 k=64 | fail d=9 k=2 | pass | differs |
| `lt_three` | pass | fail d=255 k=64 | pass | pass | pass | fail d=208 k=64 | pass | pass | yes |
| `lt_ambient_only` | pass | fail d=76 k=64 | pass | pass | pass | fail d=235 k=64 | pass | pass | yes |
| `lt_scaled_nonorm` | pass | fail d=255 k=64 | pass | pass | pass | fail d=217 k=64 | pass | pass | yes |
| `lt_scaled_norm` | pass | fail d=239 k=64 | pass | pass | pass | fail d=217 k=64 | pass | pass | yes |
| `lt_colorvertex` | pass | fail d=225 k=64 | pass | pass | pass | fail d=241 k=64 | pass | pass | yes |
| `lt_colorvertex_off` | pass | fail d=239 k=64 | pass | pass | pass | fail d=217 k=64 | pass | pass | yes |
| `lt_colorvertex_spec_src` | pass | fail d=255 k=64 | pass | pass | pass | fail d=237 k=64 | pass | pass | yes |
| `lt_colorvertex_nodiffuse` | pass | fail d=239 k=64 | pass | pass | pass | fail d=217 k=64 | pass | pass | yes |
| `lt_tex_modulate` | pass | fail d=199 k=64 | pass | pass | pass | fail d=253 k=64 | pass | pass | yes |
| `lt_persp_point` | pass | fail d=76 k=64 | pass | pass | pass | fail d=222 k=64 | pass | pass | yes |
| `lt_backface_cull_off` | pass | fail d=76 k=64 | pass | pass | pass | fail d=217 k=64 | pass | pass | yes |
| `at_greater` | pass | fail d=140 k=32 | pass | pass | pass | fail d=140 k=32 | pass | pass | yes |
| `at_greaterequal` | pass | fail d=140 k=16 | pass | pass | pass | fail d=140 k=16 | pass | pass | yes |
| `at_less` | pass | fail d=204 k=64 | pass | pass | pass | fail d=204 k=64 | pass | pass | yes |
| `at_lessequal` | pass | fail d=204 k=64 | pass | pass | pass | fail d=204 k=64 | pass | pass | yes |
| `at_notequal` | pass | fail d=140 k=64 | pass | pass | pass | fail d=140 k=64 | pass | pass | yes |
| `at_never` | pass | pass | pass | pass | pass | pass | pass | pass | yes |
| `at_xyz_diffuse` | pass | fail d=192 k=64 | pass | pass | pass | fail d=111 k=24 | pass | pass | yes |
| `ab_alpha` | pass | fail d=107 k=64 | pass | pass | pass | fail d=107 k=64 | pass | pass | yes |
| `ab_add` | pass | fail d=127 k=64 | pass | pass | pass | fail d=127 k=64 | pass | pass | yes |
| `ab_srcalpha_one` | pass | fail d=160 k=64 | pass | pass | pass | fail d=160 k=64 | pass | pass | yes |
| `ab_modulate` | pass | fail d=107 k=64 | pass | pass | pass | fail d=107 k=64 | pass | pass | yes |
| `ab_modulate2x` | pass | fail d=213 k=64 | pass | pass | pass | fail d=213 k=64 | pass | pass | yes |
| `ab_zero_srccolor` | pass | fail d=107 k=64 | pass | pass | pass | fail d=107 k=64 | pass | pass | yes |
| `ab_invdestcolor` | pass | fail d=106 k=64 | pass | pass | pass | fail d=106 k=64 | pass | pass | yes |
| `ab_destalpha` | pass | fail d=106 k=64 | pass | pass | pass | fail d=106 k=64 | pass | pass | yes |
| `ab_premul` | pass | fail d=107 k=64 | pass | pass | pass | fail d=107 k=64 | pass | pass | yes |
| `ab_bothsrcalpha` | pass | fail d=107 k=64 | pass | pass | pass | fail d=107 k=64 | pass | pass | yes |
| `ab_srcalphasat` | pass | fail d=127 k=64 | pass | pass | pass | fail d=127 k=64 | pass | pass | yes |
| `ab_revsubtract` | pass | fail d=128 k=64 | pass | pass | pass | fail d=128 k=64 | pass | pass | yes |
| `ab_subtract` | pass | fail d=85 k=24 | pass | pass | pass | fail d=85 k=24 | pass | pass | yes |
| `ab_min` | pass | fail d=128 k=64 | pass | pass | pass | fail d=128 k=64 | pass | pass | yes |
| `ab_max` | pass | fail d=85 k=24 | pass | pass | pass | fail d=85 k=24 | pass | pass | yes |
| `ab_blendfactor` | pass | fail d=53 k=56 | pass | pass | pass | fail d=53 k=56 | pass | pass | yes |
| `ab_separate_alpha` | pass | fail d=107 k=64 | pass | pass | pass | fail d=107 k=64 | pass | pass | yes |
| `ab_atest_and_blend` | pass | fail d=107 k=40 | pass | pass | pass | fail d=107 k=40 | pass | pass | yes |
| `ab_fog_blend` | fail d=20 k=64 | fail d=183 k=64 | pass | pass | fail d=20 k=56 | fail d=176 k=64 | pass | pass | yes |
| `pt_size_rhw` | fail d=80 k=16 | fail d=80 k=16 | pass | pass | fail d=239 k=16 | fail d=239 k=16 | pass | pass | yes |
| `pt_size_xyz` | fail d=80 k=16 | fail d=80 k=16 | pass | pass | fail d=239 k=16 | fail d=239 k=16 | pass | pass | yes |
| `pt_sprite_rhw` | pass | pass | pass | pass | pass | pass | pass | pass | yes |
| `pt_sprite_xyz` | pass | pass | pass | pass | pass | pass | pass | pass | yes |
| `pt_sprite_notex_coords` | pass | pass | pass | pass | pass | pass | pass | pass | yes |
| `pt_psize_rhw` | fail d=128 k=8 | fail d=128 k=8 | pass | pass | fail d=128 k=8 | fail d=128 k=8 | pass | pass | yes |
| `pt_psize_xyz` | pass | fail d=128 k=8 | pass | pass | pass | fail d=128 k=8 | pass | pass | yes |
| `pt_size_max` | pass | pass | pass | pass | pass | pass | pass | pass | yes |
| `pt_size_min` | fail d=80 k=16 | fail d=80 k=16 | pass | pass | fail d=239 k=16 | fail d=239 k=16 | pass | pass | yes |
| `pt_scale` | fail d=132 k=16 | fail d=132 k=16 | pass | pass | fail d=132 k=16 | fail d=132 k=16 | pass | pass | yes |
| `pt_scale_ab` | fail d=127 k=16 | fail d=127 k=16 | pass | pass | fail d=239 k=16 | fail d=127 k=16 | pass | pass | yes |
| `xf_world_view` | pass | fail d=230 k=20 | pass | pass | pass | fail d=249 k=20 | pass | pass | yes |
| `xf_persp` | pass | fail d=254 k=32 | pass | pass | pass | fail d=254 k=32 | pass | pass | yes |
| `xf_persp_tex` | pass | fail d=254 k=32 | pass | pass | pass | fail d=254 k=32 | pass | pass | yes |
| `xf_near_clip` | pass | pass | pass | pass | pass | pass | pass | pass | yes |
| `xf_cull` | pass | fail d=239 k=32 | pass | pass | pass | fail d=239 k=32 | pass | pass | yes |
| `xf_viewport` | pass | fail d=230 k=20 | pass | pass | pass | fail d=249 k=20 | pass | pass | yes |
| `xf_indexed_vb` | pass | fail d=252 k=52 | pass | pass | pass | fail d=254 k=52 | pass | pass | yes |
| `xf_clipplane` | pass | pass | pass | pass | pass | pass | pass | pass | yes |
| `xf_vertexblend` | pass | fail d=252 k=48 | pass | pass | pass | fail d=252 k=48 | pass | pass | yes |
| `xf_depth` | pass | fail d=255 k=58 | pass | pass | pass | fail d=255 k=58 | pass | pass | yes |
| `xf_flat_shade` | pass | fail d=255 k=64 | pass | pass | pass | fail d=255 k=64 | pass | pass | yes |
| `xf_lines` | pass | fail d=255 k=8 | pass | pass | pass | fail d=255 k=8 | pass | pass | yes |

| Run | nvidia2 before | nvidia2 after | amd1 before | amd1 after |
| --- | --- | --- | --- | --- |
| x64-hw against x64-ref | 153 of 168 | 166 of 168 | 152 of 168 | 166 of 168 |
| x86-hw against x86-ref | 153 of 168 | 166 of 168 | 152 of 168 | 166 of 168 |
| x64-swvp against x64-refswvp | 24 of 168 | 168 of 168 | 22 of 168 | 168 of 168 |
