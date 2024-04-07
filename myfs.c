#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/mount.h>
#include <linux/init.h>
#include <linux/namei.h>
#define AUFS_MAGIC 0x64668735
static struct vfsmount *myfs_mount;
static int myfs_mount_count;
static struct inode *myfs_get_inode(struct super_block *sb, int mode, dev_t dev)
{
     struct inode *inode = new_inode(sb);
     if (inode)
     {
          inode->i_mode = mode;
          inode->i_uid = current->fsuid;
          inode->i_gid = current->fsgid;
          // inode->i_blksize = 4096;
          inode->i_blocks = 0;
          inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
          switch (mode & S_IFMT)
          {
          default:
               init_special_inode(inode, mode, dev);
               break;
          case S_IFREG:
               printk("creat a  file \n");
               break;
          case S_IFDIR:
               inode->i_op = &simple_dir_inode_operations;
               inode->i_fop = &simple_dir_operations;
               printk("creat a dir file \n");
               inode->i_nlink++;
               break;
          }
     }
     return inode;
}
/* SMP-safe */
static int myfs_mknod(struct inode *dir, struct dentry *dentry,
                      int mode, dev_t dev)
{
     struct inode *inode;
     int error = -EPERM;
     if (dentry->d_inode)
          return -EEXIST;
     inode = myfs_get_inode(dir->i_sb, mode, dev);
     if (inode)
     {
          d_instantiate(dentry, inode);
          dget(dentry);
          error = 0;
     }
     return error;
}
static int myfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
     int res;
     res = myfs_mknod(dir, dentry, mode | S_IFDIR, 0);
     if (!res)
          dir->i_nlink++;
     return res;
}
static int myfs_create(struct inode *dir, struct dentry *dentry, int mode)
{
     return myfs_mknod(dir, dentry, mode | S_IFREG, 0);
}
static int myfs_fill_super(struct super_block *sb, void *data, int silent)
{
     static struct tree_descr debug_files[] = {{""}};
     return simple_fill_super(sb, AUFS_MAGIC, debug_files);
}
int myfs_get_sb(struct file_system_type *fs_type,
                int flags, const char *dev_name,
                void *data, struct vfsmount *mnt)
{
     return get_sb_single(fs_type, flags, data, myfs_fill_super, mnt);
}
static struct file_system_type au_fs_type = {
    .owner = THIS_MODULE,
    .name = "myfs",
    .get_sb = myfs_get_sb,
    .kill_sb = kill_litter_super,
};
static int myfs_create_by_name(const char *name, mode_t mode,
                               struct dentry *parent,
                               struct dentry **dentry)
{
     int error = 0;
     /* If the parent is not specified, we create it in the root.
      * We need the root dentry to do this, which is in the super
      * block. A pointer to that is in the struct vfsmount that we
      * have around.
      */
     if (!parent)
     {
          if (myfs_mount && myfs_mount->mnt_sb)
          {
               parent = myfs_mount->mnt_sb->s_root;
          }
     }
     if (!parent)
     {
          printk("Ah! can not find a parent!\n");
          return -EFAULT;
     }
     *dentry = NULL;
     mutex_lock(&parent->d_inode->i_mutex);
     *dentry = lookup_one_len(name, parent, strlen(name));
     if (!IS_ERR(dentry))
     {
          if ((mode & S_IFMT) == S_IFDIR)
               error = myfs_mkdir(parent->d_inode, *dentry, mode);
          else
               error = myfs_create(parent->d_inode, *dentry, mode);
     }
     else
          error = PTR_ERR(dentry);
     mutex_unlock(&parent->d_inode->i_mutex);
     return error;
}
struct dentry *myfs_create_file(const char *name, mode_t mode,
                                struct dentry *parent, void *data,
                                struct file_operations *fops)
{
     struct dentry *dentry = NULL;
     int error;
     printk("myfs: creating file '%s'\n", name);
     error = myfs_create_by_name(name, mode, parent, &dentry);
     if (error)
     {
          dentry = NULL;
          goto exit;
     }
     if (dentry->d_inode)
     {
          // if (data)
          // dentry->d_inode->u.generic_ip = data;
          if (fops)
               dentry->d_inode->i_fop = fops;
     }
exit:
     return dentry;
}
struct dentry *myfs_create_dir(const char *name, struct dentry *parent)
{
     return myfs_create_file(name,
                             S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO,
                             parent, NULL, NULL);
}
static int __init myfs_init(void)
{
     int retval;
     struct dentry *pslot;

     retval = register_filesystem(&au_fs_type);
     if (!retval)
     {
          myfs_mount = kern_mount(&au_fs_type);
          if (IS_ERR(myfs_mount))
          {
               printk(KERN_ERR "myfs: could not mount!\n");
               unregister_filesystem(&au_fs_type);
               return retval;
          }
     }
     pslot = myfs_create_dir("woman star", NULL);
     myfs_create_file("lbb", S_IFREG | S_IRUGO, pslot, NULL, NULL);
     myfs_create_file("fbb", S_IFREG | S_IRUGO, pslot, NULL, NULL);
     myfs_create_file("ljl", S_IFREG | S_IRUGO, pslot, NULL, NULL);
     pslot = myfs_create_dir("man star", NULL);
     myfs_create_file("ldh", S_IFREG | S_IRUGO, pslot, NULL, NULL);
     myfs_create_file("lcw", S_IFREG | S_IRUGO, pslot, NULL, NULL);
     myfs_create_file("jw", S_IFREG | S_IRUGO, pslot, NULL, NULL);
     return retval;
}
static void __exit myfs_exit(void)
{
     simple_release_fs(&myfs_mount, &myfs_mount_count);
     unregister_filesystem(&au_fs_type);
}
module_init(myfs_init);
module_exit(myfs_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("This is a simple module");
MODULE_VERSION("Ver 0.1");