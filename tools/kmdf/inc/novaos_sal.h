/* novaos_sal.h — the WDK's source annotations (SAL), which only its code
 * analysis reads: each expands to nothing, as it does for the compiler. */
#pragma once
#ifndef _Acquires_exclusive_lock_
#define _Acquires_exclusive_lock_(...)
#endif
#ifndef _Acquires_lock_
#define _Acquires_lock_(...)
#endif
#ifndef _Acquires_shared_lock_
#define _Acquires_shared_lock_(...)
#endif
#ifndef _Always_
#define _Always_(...)
#endif
#ifndef _Analysis_assume_
#define _Analysis_assume_(...)
#endif
#ifndef _Analysis_mode_
#define _Analysis_mode_(...)
#endif
#ifndef _Analysis_noreturn_
#define _Analysis_noreturn_
#endif
#ifndef _At_
#define _At_(...)
#endif
#ifndef _Benign_race_begin_
#define _Benign_race_begin_
#endif
#ifndef _Benign_race_end_
#define _Benign_race_end_
#endif
#ifndef _Called_from_function_class_
#define _Called_from_function_class_(...)
#endif
#ifndef _Check_return_
#define _Check_return_
#endif
#ifndef _Create_lock_level_
#define _Create_lock_level_(...)
#endif
#ifndef _Curr_
#define _Curr_
#endif
#ifndef _Deref_out_
#define _Deref_out_
#endif
#ifndef _Deref_out_range_
#define _Deref_out_range_(...)
#endif
#ifndef _Deref_post_maybenull_
#define _Deref_post_maybenull_
#endif
#ifndef _Dispatch_type_
#define _Dispatch_type_
#endif
#ifndef _Enum_is_bitflag_
#define _Enum_is_bitflag_
#endif
#ifndef _Field_range_
#define _Field_range_(...)
#endif
#ifndef _Field_size_
#define _Field_size_(...)
#endif
#ifndef _Field_size_bytes_
#define _Field_size_bytes_(...)
#endif
#ifndef _Field_size_bytes_opt_
#define _Field_size_bytes_opt_(...)
#endif
#ifndef _Field_size_opt_
#define _Field_size_opt_(...)
#endif
#ifndef _Frees_ptr_
#define _Frees_ptr_
#endif
#ifndef _Frees_ptr_opt_
#define _Frees_ptr_opt_
#endif
#ifndef _Function_class_
#define _Function_class_(...)
#endif
#ifndef _Guarded_by_
#define _Guarded_by_(...)
#endif
#ifndef _Has_lock_kind_
#define _Has_lock_kind_(...)
#endif
#ifndef _IRQL_always_function_max_
#define _IRQL_always_function_max_(...)
#endif
#ifndef _IRQL_always_function_min_
#define _IRQL_always_function_min_(...)
#endif
#ifndef _IRQL_is_cancel_
#define _IRQL_is_cancel_
#endif
#ifndef _IRQL_raises_
#define _IRQL_raises_(...)
#endif
#ifndef _IRQL_requires_
#define _IRQL_requires_(...)
#endif
#ifndef _IRQL_requires_max_
#define _IRQL_requires_max_(...)
#endif
#ifndef _IRQL_requires_min_
#define _IRQL_requires_min_(...)
#endif
#ifndef _IRQL_requires_same_
#define _IRQL_requires_same_
#endif
#ifndef _IRQL_restores_
#define _IRQL_restores_
#endif
#ifndef _IRQL_saves_
#define _IRQL_saves_
#endif
#ifndef _IRQL_uses_cancel_
#define _IRQL_uses_cancel_
#endif
#ifndef _In_
#define _In_
#endif
#ifndef _In_opt_
#define _In_opt_
#endif
#ifndef _In_range_
#define _In_range_(...)
#endif
#ifndef _In_reads_
#define _In_reads_(...)
#endif
#ifndef _In_reads_bytes_
#define _In_reads_bytes_(...)
#endif
#ifndef _In_reads_bytes_opt_
#define _In_reads_bytes_opt_(...)
#endif
#ifndef _In_reads_opt_
#define _In_reads_opt_(...)
#endif
#ifndef _In_reads_z_
#define _In_reads_z_(...)
#endif
#ifndef _In_z_
#define _In_z_
#endif
#ifndef _Inexpressible_
#define _Inexpressible_(...)
#endif
#ifndef _Inout_
#define _Inout_
#endif
#ifndef _Inout_opt_
#define _Inout_opt_
#endif
#ifndef _Inout_updates_
#define _Inout_updates_(...)
#endif
#ifndef _Inout_updates_bytes_
#define _Inout_updates_bytes_(...)
#endif
#ifndef _Inout_updates_bytes_opt_
#define _Inout_updates_bytes_opt_(...)
#endif
#ifndef _Inout_z_
#define _Inout_z_
#endif
#ifndef _Interlocked_operand_
#define _Interlocked_operand_
#endif
#ifndef _Kernel_acquires_resource_
#define _Kernel_acquires_resource_(...)
#endif
#ifndef _Kernel_clear_do_init_
#define _Kernel_clear_do_init_(...)
#endif
#ifndef _Kernel_float_used_
#define _Kernel_float_used_
#endif
#ifndef _Kernel_releases_resource_
#define _Kernel_releases_resource_(...)
#endif
#ifndef _Kernel_requires_resource_held_
#define _Kernel_requires_resource_held_(...)
#endif
#ifndef _Kernel_requires_resource_not_held_
#define _Kernel_requires_resource_not_held_(...)
#endif
#ifndef _Literal_
#define _Literal_
#endif
#ifndef _Lock_kind_spin_lock_
#define _Lock_kind_spin_lock_
#endif
#ifndef _Lock_level_order_
#define _Lock_level_order_(...)
#endif
#ifndef _Maybenull_
#define _Maybenull_
#endif
#ifndef _Must_inspect_impl_
#define _Must_inspect_impl_
#endif
#ifndef _Must_inspect_result_
#define _Must_inspect_result_
#endif
#ifndef _No_competing_thread_
#define _No_competing_thread_
#endif
#ifndef _Notliteral_
#define _Notliteral_
#endif
#ifndef _Notnull_
#define _Notnull_
#endif
#ifndef _NullNull_terminated_
#define _NullNull_terminated_
#endif
#ifndef _Null_terminated_
#define _Null_terminated_
#endif
#ifndef _Out_
#define _Out_
#endif
#ifndef _Out_opt_
#define _Out_opt_
#endif
#ifndef _Out_opt_bytecap_
#define _Out_opt_bytecap_(...)
#endif
#ifndef _Out_range_
#define _Out_range_(...)
#endif
#ifndef _Out_writes_
#define _Out_writes_(...)
#endif
#ifndef _Out_writes_all_
#define _Out_writes_all_(...)
#endif
#ifndef _Out_writes_bytes_
#define _Out_writes_bytes_(...)
#endif
#ifndef _Out_writes_bytes_all_
#define _Out_writes_bytes_all_(...)
#endif
#ifndef _Out_writes_bytes_all_opt_
#define _Out_writes_bytes_all_opt_(...)
#endif
#ifndef _Out_writes_bytes_opt_
#define _Out_writes_bytes_opt_(...)
#endif
#ifndef _Out_writes_bytes_to_
#define _Out_writes_bytes_to_(...)
#endif
#ifndef _Out_writes_bytes_to_opt_
#define _Out_writes_bytes_to_opt_(...)
#endif
#ifndef _Out_writes_opt_
#define _Out_writes_opt_(...)
#endif
#ifndef _Out_writes_to_
#define _Out_writes_to_(...)
#endif
#ifndef _Out_writes_z_
#define _Out_writes_z_(...)
#endif
#ifndef _Out_z_cap_
#define _Out_z_cap_(...)
#endif
#ifndef _Outptr_
#define _Outptr_
#endif
#ifndef _Outptr_opt_
#define _Outptr_opt_
#endif
#ifndef _Outptr_opt_result_bytebuffer_
#define _Outptr_opt_result_bytebuffer_(...)
#endif
#ifndef _Outptr_opt_result_maybenull_
#define _Outptr_opt_result_maybenull_
#endif
#ifndef _Outptr_result_buffer_
#define _Outptr_result_buffer_(...)
#endif
#ifndef _Outptr_result_bytebuffer_
#define _Outptr_result_bytebuffer_(...)
#endif
#ifndef _Outptr_result_bytebuffer_maybenull_
#define _Outptr_result_bytebuffer_maybenull_(...)
#endif
#ifndef _Outptr_result_maybenull_
#define _Outptr_result_maybenull_
#endif
#ifndef _Points_to_data_
#define _Points_to_data_
#endif
#ifndef _Post_
#define _Post_
#endif
#ifndef _Post_defensive_
#define _Post_defensive_
#endif
#ifndef _Post_equal_to_
#define _Post_equal_to_(...)
#endif
#ifndef _Post_invalid_
#define _Post_invalid_
#endif
#ifndef _Post_notnull_
#define _Post_notnull_
#endif
#ifndef _Post_ptr_invalid_
#define _Post_ptr_invalid_
#endif
#ifndef _Post_satisfies_
#define _Post_satisfies_(...)
#endif
#ifndef _Pre_defensive_
#define _Pre_defensive_
#endif
#ifndef _Pre_notnull_
#define _Pre_notnull_
#endif
#ifndef _Pre_satisfies_
#define _Pre_satisfies_(...)
#endif
#ifndef _Pre_unknown_
#define _Pre_unknown_
#endif
#ifndef _Pre_writable_byte_size_
#define _Pre_writable_byte_size_(...)
#endif
#ifndef _Printf_format_string_
#define _Printf_format_string_
#endif
#ifndef _Readable_bytes_
#define _Readable_bytes_(...)
#endif
#ifndef _Readable_elements_
#define _Readable_elements_(...)
#endif
#ifndef _Releases_exclusive_lock_
#define _Releases_exclusive_lock_(...)
#endif
#ifndef _Releases_lock_
#define _Releases_lock_(...)
#endif
#ifndef _Releases_shared_lock_
#define _Releases_shared_lock_(...)
#endif
#ifndef _Requires_exclusive_lock_held_
#define _Requires_exclusive_lock_held_(...)
#endif
#ifndef _Requires_lock_held_
#define _Requires_lock_held_(...)
#endif
#ifndef _Requires_lock_not_held_
#define _Requires_lock_not_held_(...)
#endif
#ifndef _Requires_shared_lock_held_
#define _Requires_shared_lock_held_(...)
#endif
#ifndef _Reserved_
#define _Reserved_
#endif
#ifndef _Ret_maybenull_
#define _Ret_maybenull_
#endif
#ifndef _Ret_maybenull_z_
#define _Ret_maybenull_z_
#endif
#ifndef _Ret_notnull_
#define _Ret_notnull_
#endif
#ifndef _Ret_range_
#define _Ret_range_(...)
#endif
#ifndef _Ret_writes_bytes_maybenull_
#define _Ret_writes_bytes_maybenull_(...)
#endif
#ifndef _Ret_z_
#define _Ret_z_
#endif
#ifndef _Strict_type_match_
#define _Strict_type_match_
#endif
#ifndef _Struct_size_bytes_
#define _Struct_size_bytes_(...)
#endif
#ifndef _Success_
#define _Success_(...)
#endif
#ifndef _Unreferenced_parameter_
#define _Unreferenced_parameter_(...)
#endif
#ifndef _Use_decl_anno_impl_
#define _Use_decl_anno_impl_
#endif
#ifndef _Use_decl_annotations_
#define _Use_decl_annotations_
#endif
#ifndef _When_
#define _When_(...)
#endif
#ifndef _Writable_bytes_
#define _Writable_bytes_(...)
#endif
#ifndef _Writable_elements_
#define _Writable_elements_(...)
#endif
#ifndef __analysis_assume
#define __analysis_assume(...)
#endif
#ifndef __bcount
#define __bcount(...)
#endif
#ifndef __checkReturn
#define __checkReturn
#endif
#ifndef __deref
#define __deref
#endif
#ifndef __deref_in
#define __deref_in
#endif
#ifndef __deref_inout
#define __deref_inout
#endif
#ifndef __deref_opt_out
#define __deref_opt_out
#endif
#ifndef __deref_opt_out_bcount
#define __deref_opt_out_bcount(...)
#endif
#ifndef __deref_out
#define __deref_out
#endif
#ifndef __deref_out_bcount
#define __deref_out_bcount(...)
#endif
#ifndef __deref_out_bcount_opt
#define __deref_out_bcount_opt(...)
#endif
#ifndef __deref_out_opt
#define __deref_out_opt
#endif
#ifndef __deref_out_range
#define __deref_out_range(...)
#endif
#ifndef __drv_aliasesMem
#define __drv_aliasesMem
#endif
#ifndef __drv_allocatesMem
#define __drv_allocatesMem(...)
#endif
#ifndef __drv_arg
#define __drv_arg(...)
#endif
#ifndef __drv_deref
#define __drv_deref(...)
#endif
#ifndef __drv_dispatchType
#define __drv_dispatchType(...)
#endif
#ifndef __drv_formatString
#define __drv_formatString(...)
#endif
#ifndef __drv_freesMem
#define __drv_freesMem(...)
#endif
#ifndef __drv_functionClass
#define __drv_functionClass(...)
#endif
#ifndef __drv_maxFunctionIRQL
#define __drv_maxFunctionIRQL(...)
#endif
#ifndef __drv_maxIRQL
#define __drv_maxIRQL(...)
#endif
#ifndef __drv_minIRQL
#define __drv_minIRQL(...)
#endif
#ifndef __drv_nonConstant
#define __drv_nonConstant
#endif
#ifndef __drv_raisesIRQL
#define __drv_raisesIRQL(...)
#endif
#ifndef __drv_reportError
#define __drv_reportError(...)
#endif
#ifndef __drv_requiresIRQL
#define __drv_requiresIRQL(...)
#endif
#ifndef __drv_restoresIRQL
#define __drv_restoresIRQL
#endif
#ifndef __drv_restoresIRQLGlobal
#define __drv_restoresIRQLGlobal(...)
#endif
#ifndef __drv_sameIRQL
#define __drv_sameIRQL
#endif
#ifndef __drv_savesIRQL
#define __drv_savesIRQL
#endif
#ifndef __drv_savesIRQLGlobal
#define __drv_savesIRQLGlobal(...)
#endif
#ifndef __drv_setsIRQL
#define __drv_setsIRQL(...)
#endif
#ifndef __drv_strictTypeMatch
#define __drv_strictTypeMatch(...)
#endif
#ifndef __drv_typeCond
#define __drv_typeCond
#endif
#ifndef __drv_typeConst
#define __drv_typeConst
#endif
#ifndef __drv_typeExpr
#define __drv_typeExpr
#endif
#ifndef __drv_useCancelIRQL
#define __drv_useCancelIRQL
#endif
#ifndef __drv_valueIs
#define __drv_valueIs(...)
#endif
#ifndef __drv_when
#define __drv_when(...)
#endif
#ifndef __field_bcount
#define __field_bcount(...)
#endif
#ifndef __field_ecount
#define __field_ecount(...)
#endif
#ifndef __in
#define __in
#endif
#ifndef __in_bcount
#define __in_bcount(...)
#endif
#ifndef __in_bcount_opt
#define __in_bcount_opt(...)
#endif
#ifndef __in_bound
#define __in_bound
#endif
#ifndef __in_ecount
#define __in_ecount(...)
#endif
#ifndef __in_opt
#define __in_opt
#endif
#ifndef __in_xcount
#define __in_xcount(...)
#endif
#ifndef __in_xcount_opt
#define __in_xcount_opt(...)
#endif
#ifndef __inout
#define __inout
#endif
#ifndef __inout_bcount
#define __inout_bcount(...)
#endif
#ifndef __inout_opt
#define __inout_opt
#endif
#ifndef __inout_xcount
#define __inout_xcount(...)
#endif
#ifndef __nullnullterminated
#define __nullnullterminated
#endif
#ifndef __nullterminated
#define __nullterminated
#endif
#ifndef __out
#define __out
#endif
#ifndef __out_bcount
#define __out_bcount(...)
#endif
#ifndef __out_bcount_full
#define __out_bcount_full(...)
#endif
#ifndef __out_bcount_full_opt
#define __out_bcount_full_opt(...)
#endif
#ifndef __out_bcount_opt
#define __out_bcount_opt(...)
#endif
#ifndef __out_bcount_part
#define __out_bcount_part(...)
#endif
#ifndef __out_bcount_part_opt
#define __out_bcount_part_opt(...)
#endif
#ifndef __out_ecount_full
#define __out_ecount_full(...)
#endif
#ifndef __out_ecount_opt
#define __out_ecount_opt(...)
#endif
#ifndef __out_opt
#define __out_opt
#endif
#ifndef __out_range
#define __out_range(...)
#endif
#ifndef __out_xcount
#define __out_xcount(...)
#endif
#ifndef __range
#define __range(...)
#endif
#ifndef __success
#define __success(...)
#endif
