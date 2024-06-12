#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/sched.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>

extern const struct inode_operations myfs_file_inode_operations;

static unsigned long myfs_mmu_get_unmapped_area(struct file *file,
		unsigned long addr, unsigned long len, unsigned long pgoff,
		unsigned long flags)
{
	return current->mm->get_unmapped_area(file, addr, len, pgoff, flags);
}

const struct file_operations myfs_file_operations = {
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.fsync		= noop_fsync,
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.llseek		= generic_file_llseek,
	.get_unmapped_area	= myfs_mmu_get_unmapped_area,
};

const struct inode_operations myfs_file_inode_operations = {
	.setattr	= simple_setattr,
	.getattr	= simple_getattr,
};

struct myfs_mount_opts {
	umode_t mode;
};

struct myfs_fs_info {
	struct myfs_mount_opts mount_opts;
};

#define RAMFS_DEFAULT_MODE	0755

static const struct super_operations myfs_ops;
static const struct inode_operations myfs_dir_inode_operations;

struct inode *myfs_get_inode(struct super_block *sb,
				const struct inode *dir, umode_t mode, dev_t dev)
{
	struct inode * inode = new_inode(sb);

	if (inode) {
		inode->i_ino = get_next_ino();
		inode_init_owner(&init_user_ns, inode, dir, mode);
		inode->i_mapping->a_ops = &ram_aops;
		mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
		mapping_set_unevictable(inode->i_mapping);
		inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_op = &myfs_file_inode_operations;
			inode->i_fop = &myfs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &myfs_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;

			/* directory inodes start off with i_nlink == 2 (for "." entry) */
			inc_nlink(inode);
			break;
		case S_IFLNK:
			inode->i_op = &page_symlink_inode_operations;
			inode_nohighmem(inode);
			break;
		}
	}
	return inode;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
/* SMP-safe */
static int
myfs_mknod(struct user_namespace *mnt_userns, struct inode *dir,
	    struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode * inode = myfs_get_inode(dir->i_sb, dir, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry);	/* Extra count - pin the dentry in core */
		error = 0;
		dir->i_mtime = dir->i_ctime = current_time(dir);
	}
	return error;
}

static int myfs_mkdir(struct user_namespace *mnt_userns, struct inode *dir,
		       struct dentry *dentry, umode_t mode)
{
	int retval = myfs_mknod(&init_user_ns, dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);

	printk(KERN_INFO "myfs: create dir %s success!\n", dentry->d_iname);
	return retval;
}

static int myfs_create(struct user_namespace *mnt_userns, struct inode *dir,
			struct dentry *dentry, umode_t mode, bool excl)
{
	int ret = 0;
	ret =  myfs_mknod(&init_user_ns, dir, dentry, mode | S_IFREG, 0);
	printk(KERN_INFO "myfs: create file %s success!\n", dentry->d_iname);
	return ret;
}

static int myfs_symlink(struct user_namespace *mnt_userns, struct inode *dir,
			 struct dentry *dentry, const char *symname)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = myfs_get_inode(dir->i_sb, dir, S_IFLNK|S_IRWXUGO, 0);
	if (inode) {
		int l = strlen(symname)+1;
		error = page_symlink(inode, symname, l);
		if (!error) {
			d_instantiate(dentry, inode);
			dget(dentry);
			dir->i_mtime = dir->i_ctime = current_time(dir);
		} else
			iput(inode);
	}
	return error;
}

static int myfs_tmpfile(struct user_namespace *mnt_userns,
			 struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct inode *inode;

	inode = myfs_get_inode(dir->i_sb, dir, mode, 0);
	if (!inode)
		return -ENOSPC;
	d_tmpfile(dentry, inode);
	return 0;
}

static const struct inode_operations myfs_dir_inode_operations = {
	.create		= myfs_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= simple_unlink,
	.symlink	= myfs_symlink,
	.mkdir		= myfs_mkdir,
	.rmdir		= simple_rmdir,
	.mknod		= myfs_mknod,
	.rename		= simple_rename,
	.tmpfile	= myfs_tmpfile,
};

/*
 * Display the mount options in /proc/mounts.
 */
static int myfs_show_options(struct seq_file *m, struct dentry *root)
{
	struct myfs_fs_info *fsi = root->d_sb->s_fs_info;

	if (fsi->mount_opts.mode != RAMFS_DEFAULT_MODE)
		seq_printf(m, ",mode=%o", fsi->mount_opts.mode);
	return 0;
}

static const struct super_operations myfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.show_options	= myfs_show_options,
};

enum myfs_param {
	Opt_mode,
};

const struct fs_parameter_spec myfs_fs_parameters[] = {
	fsparam_u32oct("mode",	Opt_mode),
	{}
};

static int myfs_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct fs_parse_result result;
	struct myfs_fs_info *fsi = fc->s_fs_info;
	int opt;

	opt = fs_parse(fc, myfs_fs_parameters, param, &result);
	if (opt < 0) {
		/*
		 * We might like to report bad mount options here;
		 * but traditionally myfs has ignored all mount options,
		 * and as it is used as a !CONFIG_SHMEM simple substitute
		 * for tmpfs, better continue to ignore other mount options.
		 */
		if (opt == -ENOPARAM)
			opt = 0;
		return opt;
	}

	switch (opt) {
	case Opt_mode:
		fsi->mount_opts.mode = result.uint_32 & S_IALLUGO;
		break;
	}

	return 0;
}

static int myfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct myfs_fs_info *fsi = sb->s_fs_info;
	struct inode *inode;

	sb->s_maxbytes		= MAX_LFS_FILESIZE;
	sb->s_blocksize		= PAGE_SIZE;
	sb->s_blocksize_bits	= PAGE_SHIFT;
	sb->s_magic		= RAMFS_MAGIC;
	sb->s_op		= &myfs_ops;
	sb->s_time_gran		= 1;

	inode = myfs_get_inode(sb, NULL, S_IFDIR | fsi->mount_opts.mode, 0);
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		return -ENOMEM;

	return 0;
}

static int myfs_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, myfs_fill_super);
}

static void myfs_free_fc(struct fs_context *fc)
{
	kfree(fc->s_fs_info);
}

static const struct fs_context_operations myfs_context_ops = {
	.free		= myfs_free_fc,
	.parse_param	= myfs_parse_param,
	.get_tree	= myfs_get_tree,
};

int myfs_init_fs_context(struct fs_context *fc)
{
	struct myfs_fs_info *fsi;

	fsi = kzalloc(sizeof(*fsi), GFP_KERNEL);
	if (!fsi)
		return -ENOMEM;

	fsi->mount_opts.mode = RAMFS_DEFAULT_MODE;
	fc->s_fs_info = fsi;
	fc->ops = &myfs_context_ops;
	return 0;
}

static void myfs_kill_sb(struct super_block *sb)
{
	kfree(sb->s_fs_info);
	kill_litter_super(sb);
}

static struct file_system_type myfs_fs_type = {
     .owner = THIS_MODULE,
	.name		= "myfs",
	.init_fs_context = myfs_init_fs_context,
	.parameters	= myfs_fs_parameters,
	.kill_sb	= myfs_kill_sb,
	.fs_flags	= FS_USERNS_MOUNT,
};

static int __init init_myfs_fs(void)
{
	int ret;
	ret = register_filesystem(&myfs_fs_type);
	printk(KERN_INFO "myfs: install myfs success!\n");
	return ret;
}

static void __exit exit_myfs_fs(void)
{
     unregister_filesystem(&myfs_fs_type);
	 printk(KERN_INFO "myfs: uninstall myfs success!\n");
}

module_init(init_myfs_fs);
module_exit(exit_myfs_fs);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("This is a simple module");
MODULE_VERSION("Ver 0.1");