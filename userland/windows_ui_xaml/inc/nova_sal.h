/* NovaOS: the SAL annotations Windows SDK headers declare and MinGW-w64's
 * sal.h leaves out (they only matter to static analysis) */
#pragma once
#include <sal.h>
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
#ifndef _Analysis_noreturn_
#define _Analysis_noreturn_
#endif
#ifndef _At_
#define _At_(...)
#endif
#ifndef _COM_Outptr_
#define _COM_Outptr_
#endif
#ifndef _COM_Outptr_opt_
#define _COM_Outptr_opt_
#endif
#ifndef _COM_Outptr_opt_result_maybenull_
#define _COM_Outptr_opt_result_maybenull_
#endif
#ifndef _COM_Outptr_result_maybenull_
#define _COM_Outptr_result_maybenull_
#endif
#ifndef _Check_return_
#define _Check_return_
#endif
#ifndef _Const_
#define _Const_
#endif
#ifndef _Deref_out_
#define _Deref_out_
#endif
#ifndef _Deref_out_opt_
#define _Deref_out_opt_
#endif
#ifndef _Deref_out_range_
#define _Deref_out_range_(...)
#endif
#ifndef _Deref_post_opt_count_
#define _Deref_post_opt_count_(...)
#endif
#ifndef _Deref_pre_z_
#define _Deref_pre_z_
#endif
#ifndef _Field_size_
#define _Field_size_(...)
#endif
#ifndef _Field_size_bytes_
#define _Field_size_bytes_(...)
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
#ifndef _Guarded_by_
#define _Guarded_by_(...)
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
#ifndef _IRQL_restores_
#define _IRQL_restores_
#endif
#ifndef _IRQL_saves_
#define _IRQL_saves_
#endif
#ifndef _In_
#define _In_(...)
#endif
#ifndef _In_bytecount_x_
#define _In_bytecount_x_(...)
#endif
#ifndef _In_count_
#define _In_count_(...)
#endif
#ifndef _In_opt_
#define _In_opt_
#endif
#ifndef _In_opt_z_
#define _In_opt_z_
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
#ifndef _In_reads_or_z_
#define _In_reads_or_z_(...)
#endif
#ifndef _In_reads_to_ptr_opt_
#define _In_reads_to_ptr_opt_(...)
#endif
#ifndef _In_reads_z_
#define _In_reads_z_(...)
#endif
#ifndef _In_z_
#define _In_z_
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
#ifndef _Inout_updates_opt_
#define _Inout_updates_opt_(...)
#endif
#ifndef _Inout_updates_opt_z_
#define _Inout_updates_opt_z_(...)
#endif
#ifndef _Inout_updates_z_
#define _Inout_updates_z_(...)
#endif
#ifndef _Inout_z_
#define _Inout_z_
#endif
#ifndef _Maybenull_
#define _Maybenull_
#endif
#ifndef _Maybenull_impl_
#define _Maybenull_impl_
#endif
#ifndef _Must_inspect_result_
#define _Must_inspect_result_
#endif
#ifndef _Notnull_
#define _Notnull_
#endif
#ifndef _Null_terminated_
#define _Null_terminated_
#endif
#ifndef _Out_
#define _Out_
#endif
#ifndef _Out_bytecap_x_
#define _Out_bytecap_x_(...)
#endif
#ifndef _Out_opt_
#define _Out_opt_
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
#ifndef _Out_writes_bytes_opt_
#define _Out_writes_bytes_opt_(...)
#endif
#ifndef _Out_writes_bytes_to_
#define _Out_writes_bytes_to_(...)
#endif
#ifndef _Out_writes_opt_
#define _Out_writes_opt_(...)
#endif
#ifndef _Out_writes_to_
#define _Out_writes_to_(...)
#endif
#ifndef _Out_writes_to_opt_
#define _Out_writes_to_opt_(...)
#endif
#ifndef _Out_writes_to_ptr_
#define _Out_writes_to_ptr_(...)
#endif
#ifndef _Out_writes_z_
#define _Out_writes_z_(...)
#endif
#ifndef _Outptr_
#define _Outptr_
#endif
#ifndef _Outptr_opt_
#define _Outptr_opt_
#endif
#ifndef _Outptr_opt_result_buffer_
#define _Outptr_opt_result_buffer_(...)
#endif
#ifndef _Outptr_opt_result_maybenull_
#define _Outptr_opt_result_maybenull_
#endif
#ifndef _Outptr_result_buffer_
#define _Outptr_result_buffer_(...)
#endif
#ifndef _Outptr_result_buffer_all_
#define _Outptr_result_buffer_all_(...)
#endif
#ifndef _Outptr_result_buffer_all_maybenull_
#define _Outptr_result_buffer_all_maybenull_(...)
#endif
#ifndef _Outptr_result_buffer_maybenull_
#define _Outptr_result_buffer_maybenull_(...)
#endif
#ifndef _Outptr_result_bytebuffer_
#define _Outptr_result_bytebuffer_(...)
#endif
#ifndef _Outptr_result_maybenull_
#define _Outptr_result_maybenull_
#endif
#ifndef _Outptr_result_maybenull_z_
#define _Outptr_result_maybenull_z_
#endif
#ifndef _Outptr_result_nullonfailure_
#define _Outptr_result_nullonfailure_
#endif
#ifndef _Outptr_result_z_
#define _Outptr_result_z_
#endif
#ifndef _Param_
#define _Param_(...)
#endif
#ifndef _Post_equals_last_error_
#define _Post_equals_last_error_
#endif
#ifndef _Post_notnull_
#define _Post_notnull_
#endif
#ifndef _Post_null_
#define _Post_null_
#endif
#ifndef _Post_ptr_invalid_
#define _Post_ptr_invalid_
#endif
#ifndef _Post_satisfies_
#define _Post_satisfies_(...)
#endif
#ifndef _Post_writable_byte_size_
#define _Post_writable_byte_size_(...)
#endif
#ifndef _Post_z_
#define _Post_z_
#endif
#ifndef _Pre_defensive_
#define _Pre_defensive_
#endif
#ifndef _Pre_maybenull_
#define _Pre_maybenull_
#endif
#ifndef _Pre_opt_valid_
#define _Pre_opt_valid_
#endif
#ifndef _Pre_satisfies_
#define _Pre_satisfies_(...)
#endif
#ifndef _Pre_valid_
#define _Pre_valid_
#endif
#ifndef _Printf_format_string_
#define _Printf_format_string_
#endif
#ifndef _Releases_lock_
#define _Releases_lock_(...)
#endif
#ifndef _Reserved_
#define _Reserved_
#endif
#ifndef _Result_nullonfailure_
#define _Result_nullonfailure_
#endif
#ifndef _Ret_maybenull_
#define _Ret_maybenull_
#endif
#ifndef _Ret_notnull_
#define _Ret_notnull_
#endif
#ifndef _Ret_opt_bytecap_
#define _Ret_opt_bytecap_(...)
#endif
#ifndef _Ret_range_
#define _Ret_range_(...)
#endif
#ifndef _Ret_writes_
#define _Ret_writes_(...)
#endif
#ifndef _Ret_z_
#define _Ret_z_
#endif
#ifndef _Return_type_success_
#define _Return_type_success_(...)
#endif
#ifndef _Success_
#define _Success_(...)
#endif
#ifndef _Success_return_
#define _Success_return_
#endif
#ifndef _Success_true_
#define _Success_true_
#endif
#ifndef _Translates_NTSTATUS_to_HRESULT_
#define _Translates_NTSTATUS_to_HRESULT_(...)
#endif
#ifndef _Translates_Win32_to_HRESULT_
#define _Translates_Win32_to_HRESULT_(...)
#endif
#ifndef _Translates_last_error_to_HRESULT_
#define _Translates_last_error_to_HRESULT_
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

/* the older (specstrings.h) annotations */
#ifndef __analysis_assume
#define __analysis_assume(...)
#endif
#ifndef __bcount
#define __bcount(...)
#endif
#ifndef __control_entrypoint
#define __control_entrypoint(...)
#endif
#ifndef __deref
#define __deref
#endif
#ifndef __deref_ecount
#define __deref_ecount(...)
#endif
#ifndef __deref_inout_ecount
#define __deref_inout_ecount(...)
#endif
#ifndef __drv_freesMem
#define __drv_freesMem(...)
#endif
#ifndef __ecount_opt
#define __ecount_opt(...)
#endif
#ifndef __fallthrough
#define __fallthrough
#endif
#ifndef __in_ecount_z
#define __in_ecount_z(...)
#endif
#ifndef __in_ecount_z_opt
#define __in_ecount_z_opt(...)
#endif
#ifndef __in_xcount
#define __in_xcount(...)
#endif
#ifndef __maybenull
#define __maybenull
#endif
#ifndef __out_bcount_full_opt
#define __out_bcount_full_opt(...)
#endif
#ifndef __override
#define __override
#endif
#ifndef __pre_deref_except_maybenull
#define __pre_deref_except_maybenull
#endif
#ifndef __range
#define __range(...)
#endif
