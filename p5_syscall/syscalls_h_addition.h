/* EXCERPT ONLY — this is NOT the complete syscalls.h file.
 * The real file at include/linux/syscalls.h has hundreds of existing
 * declarations. This is the ONE line we added, near the other
 * asmlinkage syscall prototypes. Do not replace the real file with
 * just this line.
 */

asmlinkage long sys_setnice_logged(int nice_val);
