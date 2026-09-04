/* EXCERPT ONLY: this is not a complete, standalone-compilable file.
 * This block was appended to kernel/sys.c inside the Linux 6.14 source
 * tree, near the other SYSCALL_DEFINE blocks. It relies on kernel-only
 * symbols (struct task_struct, current, task_nice, set_user_nice,
 * printk, KERN_INFO, EINVAL) that only exist inside the kernel build —
 * running 'gcc' on this file alone will NOT compile. It only works as
 * part of a full kernel rebuild.
 */

/*
 * setnice_logged: change the calling process's nice value and log it
 * @nice_val: requested nice value, must be in [-20, 19]
 *
 * Returns 0 on success, -EINVAL if nice_val is out of range.
 */
SYSCALL_DEFINE1(setnice_logged, int, nice_val)
{
	long old_nice;
	struct task_struct *task = current;  /* the calling process */

	if (nice_val < -20 || nice_val > 19)
		return -EINVAL;

	old_nice = task_nice(task);          /* read current nice value */

	set_user_nice(task, nice_val);       /* apply the new nice value */

	printk(KERN_INFO
	       "setnice_logged: PID=%d comm=%s old_nice=%ld new_nice=%d\n",
	       task->pid, task->comm, old_nice, nice_val);

	return 0;
}
