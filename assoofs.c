#include <linux/module.h>       /* Needed by all modules */
#include <linux/kernel.h>       /* Needed for KERN_INFO  */
#include <linux/init.h>         /* Needed for the macros */
#include <linux/fs.h>           /* libfs stuff           */
#include <asm/uaccess.h>        /* copy_to_user          */
#include <linux/buffer_head.h>  /* buffer_head           */
#include <linux/slab.h>         /* kmem_cache            */
#include "assoofs.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mario Sánchez Melcón");

struct inode *assoofs_get_inode(struct super_block *sb, uint64_t inodeN);
struct assoofs_inode_info *assoofs_get_inode_info (struct super_block *sb, uint64_t inodeN);
static int assoofs_create_fs_object (struct inode *dir, struct dentry *dentry, umode_t mode);

void assoofs_sb_sync(struct super_block *sb){
	struct buffer_head *bH;
	struct assoofs_super_block_info *sb_info;
	
	printk(KERN_INFO "SYNC FUNCTION\n");

	bH = sb_bread(sb,ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);
	
	sb_info=(struct assoofs_super_block_info *)bH->b_data;

	bH->b_data = (char *)sb;
	mark_buffer_dirty(bH);
	sync_dirty_buffer(bH);
	brelse(bH);
}

int assoofs_sb_get_free_block(struct super_block *sb, uint64_t * n){

	struct assoofs_super_block_info *sb_info;
	struct buffer_head *b;
	int i;
	printk(KERN_INFO "GET FREE BLOCK FUNCTION\n");
	sb_info= sb->s_fs_info;
	
	for(i=ASSOOFS_RESERVED_INODES;i<ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED;i++){
		
		if(sb_info->free_blocks & (1<<i)){
			break;
		}

	}
	
	if(i==ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED){
		printk(KERN_ERR "0 FREE SPACE\n");
		return -ENOSPC;
	}

	*n = i;

	sb_info->free_blocks &= ~(1 << i);

	assoofs_sb_sync(sb);

	brelse(b);
	return 0;
}

void assoofs_inode_add(struct super_block *sb, struct assoofs_inode_info *inodo){

	struct buffer_head *bH;
	struct assoofs_super_block_info *sb_info;
	struct assoofs_inode_info *inodo_iterador;
	printk(KERN_INFO "ADD INODE FUNCTION\n");

	sb_info= sb->s_fs_info;

	bH = sb_bread(sb,ASSOOFS_INODESTORE_BLOCK_NUMBER);
	BUG_ON(!bH);

	inodo_iterador = (struct assoofs_inode_info *)bH->b_data;
	inodo_iterador += sb_info->inodes_count;

	memcpy(inodo_iterador, inodo, sizeof(struct assoofs_inode_info));
	sb_info->inodes_count++;

	mark_buffer_dirty(bH);
	assoofs_sb_sync(sb);
	brelse(bH);
}

int assoofs_inode_save(struct super_block *sb, struct assoofs_inode_info *inodo){

	struct assoofs_inode_info *inodo_iterador;
	struct buffer_head *bH;
	printk(KERN_INFO "SAVE INODE FUNCTION\n");	
	
	bH = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
	inodo_iterador = assoofs_get_inode_info(sb, inodo->inode_no);

	if(likely(inodo_iterador)){
		memcpy(inodo_iterador, inodo, sizeof(*inodo_iterador));
		mark_buffer_dirty(bH);
		sync_dirty_buffer(bH);
	} 
	else{
		printk(KERN_ERR "ERROR SAVE INODE\n");
		return -EIO;
	}		

	brelse(bH);
	return 0;

}

ssize_t assoofs_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos){

	struct buffer_head *bH;
	struct inode *inodo;
	struct assoofs_inode_info *inodo_info;
	struct super_block *sb;	
	char *buffer;
	size_t nBytes;
	
	printk(KERN_INFO "READ FUNCTION\n");
	
	inodo=filp->f_path.dentry->d_inode;
	inodo_info=inodo->i_private;

	if(*ppos>=inodo_info->file_size){
		return 0;
	}
	
	sb=inodo->i_sb;
	bH=sb_bread(sb,inodo_info->data_block_number);

	buffer=(char *) bH->b_data;
	
	nBytes=min(len,(size_t) inodo_info->file_size);;
	/*if(len>inodo_info->file_size){
		nBytes=(size_t) inodo_info->file_size;
	}*/
	
	copy_to_user(buf,buffer,nBytes);
	
	*ppos+=nBytes;
	inodo_info->file_size = *ppos;

	brelse(bH);
	return nBytes;
}

ssize_t assoofs_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos){

	struct buffer_head *bH;
	struct inode *inodo;
	struct assoofs_inode_info *inodo_info;
	struct super_block *sb;	
	char *buffer;

	printk(KERN_INFO "WRITE FUNCTION.\n");

	inodo=filp->f_path.dentry->d_inode;
	inodo_info=inodo->i_private;

	sb=inodo->i_sb;
	bH=sb_bread(sb,inodo_info->data_block_number);

	bH+=*ppos;

	buffer=(char *) bH->b_data;	


	copy_from_user(buffer,buf,len);

	mark_buffer_dirty(bH);
	sync_dirty_buffer(bH);

	*ppos+=len;
	inodo_info->file_size +=len;

	brelse(bH);
	return len;
}

static int assoofs_iterate(struct file *filp, struct dir_context *ctx){

	int i;
	struct buffer_head *bH;
	struct inode *inodo;
	struct assoofs_inode_info *inodo_info;
	struct assoofs_dir_record_entry *record;
	struct super_block *sb;
	
	printk(KERN_INFO "ITERATE FUNCTION\n");

	inodo=filp->f_path.dentry->d_inode;
	inodo_info=inodo->i_private;

	if(ctx->pos){
		return 0;
	}

	sb=inodo->i_sb;

	bH=sb_bread(sb,inodo_info->data_block_number);
	record=(struct assoofs_dir_record_entry *) bH->b_data;

	for(i=0;i<inodo_info->dir_children_count;i++){

		dir_emit(ctx,record->filename,255,record->inode_no,DT_UNKNOWN);
		ctx->pos +=sizeof(struct assoofs_dir_record_entry);
	record++;

	}

	brelse(bH);
	return 0;	
}

const  struct  file_operations  assoofs_file_operations = {
	.read = assoofs_read ,
	.write = assoofs_write ,
};

const  struct  file_operations  assoofs_dir_operations = {
	.owner = THIS_MODULE ,
	.iterate = assoofs_iterate ,
};

static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl){

	printk(KERN_INFO "CREATE FUNCTION\n");
	return assoofs_create_fs_object(dir, dentry, mode);
}

struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry,  unsigned int flags){	
	struct buffer_head *bh;
	struct assoofs_dir_record_entry *record;
	struct assoofs_inode_info *parent_info;
	struct inode *inodo;
	struct super_block *sb = parent_inode->i_sb;
	int i;

	printk(KERN_INFO "LOOKUP FUNCTION\n");
	parent_info=parent_inode->i_private;

	bh=sb_bread(sb,parent_info->data_block_number);

	record = (struct assoofs_dir_record_entry *) bh->b_data;

	for(i=0;i<parent_info->dir_children_count;i++){
		
		if(strcmp(child_dentry->d_name.name,record->filename)==0){

			inodo = assoofs_get_inode(sb,record->inode_no);
			inode_init_owner(inodo,parent_inode, ((struct assoofs_inode_info *)inodo->i_private)->mode);
			d_add(child_dentry, inodo);
			brelse(bh);
			return NULL;

		}
		record++;
	}

	printk("NO SUCH INODE ERROR\n");
	brelse(bh);
	return NULL;
};

static int assoofs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode){
	
	printk(KERN_INFO "MKDIR FUNCTION\n");
	return assoofs_create_fs_object(dir, dentry, S_IFDIR | mode);
}

static  struct  inode_operations  assoofs_inode_ops = {
	.create = assoofs_create ,
	.lookup = assoofs_lookup ,
	.mkdir = assoofs_mkdir ,
};

static const struct super_operations assoofs_sops = {
	.drop_inode= generic_delete_inode,
};

int assoofs_fill_super(struct super_block *sb, void *data, int silent){

	struct buffer_head *bH;
	struct assoofs_super_block_info *superBlock_bH;
	struct inode *nodo_raiz;
	
	bH = sb_bread(sb,ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);
	superBlock_bH=(struct assoofs_super_block_info *)bH->b_data;

	if(superBlock_bH->magic != ASSOOFS_MAGIC){
		printk(KERN_INFO "Numero mágico erroneo");		
		return -1;
	}
	if(superBlock_bH->block_size != ASSOOFS_DEFAULT_BLOCK_SIZE){
		printk(KERN_INFO "Tamaño de bloque erroneo");		
		return -1;
	}

	sb->s_magic = ASSOOFS_MAGIC;
	sb->s_fs_info = superBlock_bH;
	sb->s_maxbytes = ASSOOFS_DEFAULT_BLOCK_SIZE;
	sb->s_op = &assoofs_sops;
	
	nodo_raiz = new_inode(sb);
	inode_init_owner(nodo_raiz,NULL,S_IFDIR);
	
	nodo_raiz->i_ino = ASSOOFS_ROOTDIR_INODE_NUMBER;
	nodo_raiz->i_sb = sb;
	nodo_raiz->i_op =&assoofs_inode_ops;
	nodo_raiz->i_fop = &assoofs_dir_operations;
	nodo_raiz->i_atime=CURRENT_TIME;
	nodo_raiz->i_mtime=CURRENT_TIME;
	nodo_raiz->i_ctime=CURRENT_TIME;

	nodo_raiz->i_private = assoofs_get_inode_info(sb,ASSOOFS_ROOTDIR_INODE_NUMBER);

	sb->s_root = d_make_root(nodo_raiz);
	
	return 0;
}

struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t numero_inodo){

		struct buffer_head *bH;
		struct assoofs_inode_info *inode_info;
		struct assoofs_super_block_info *sb_info;
		int i;
		
		bH=sb_bread(sb,ASSOOFS_INODESTORE_BLOCK_NUMBER);
		inode_info=(struct assoofs_inode_info *)bH->b_data;
		sb_info=sb->s_fs_info;

		for(i=0;i<sb_info->inodes_count;i++){
			if(numero_inodo==inode_info->inode_no){
				break;
			}
			inode_info++;
		}
		brelse(bH);
		return inode_info;
}

struct inode *assoofs_get_inode(struct super_block *sb, uint64_t numero_inodo){

	struct assoofs_inode_info *inode_info;
	struct inode *inodo;

	inode_info = assoofs_get_inode_info(sb,numero_inodo);
	inodo=new_inode(sb);
	inodo->i_sb = sb;
	inodo->i_op = &assoofs_inode_ops;
	inodo->i_atime = CURRENT_TIME;
	inodo->i_mtime = CURRENT_TIME;
	inodo->i_ctime = CURRENT_TIME;
	inodo->i_private = inode_info;

	if(S_ISREG(inode_info->mode)){
		inodo->i_fop = &assoofs_file_operations;
	}
	else if(S_ISDIR(inode_info->mode)){
		inodo->i_fop = &assoofs_dir_operations;
	}
	return inodo;
}

static int assoofs_create_fs_object (struct inode *dir, struct dentry *dentry, umode_t mode){

	struct inode *inodo;	
	struct assoofs_inode_info *inodo_info;
	struct assoofs_inode_info *parent_info;

	struct super_block *sb;
	struct assoofs_super_block_info *sb_info;

	struct buffer_head *bH;

	struct assoofs_dir_record_entry *record;
	
	uint64_t num;
	int aux;
	
	printk(KERN_INFO "CREATE FS OBJECT FUNCTION\n");
	sb = dir->i_sb;
	sb_info = sb->s_fs_info;

	num = sb_info->inodes_count;

	inodo = new_inode(sb);
	inodo->i_sb = sb;
	inodo->i_op = &assoofs_inode_ops;
	inodo->i_atime=CURRENT_TIME;
	inodo->i_mtime=CURRENT_TIME;
	inodo->i_ctime=CURRENT_TIME;
	inodo->i_ino = (num + ASSOOFS_START_INO - ASSOOFS_RESERVED_INODES + 1);

	inodo_info = assoofs_get_inode_info (sb, inodo -> i_ino);
	inodo_info->inode_no = inodo->i_ino;	
	inodo_info->mode = mode;

	inodo-> i_private = inodo_info;

	if(S_ISDIR(mode)){
		printk(KERN_INFO "Petición de nuevo directorio\n");
		inodo_info->dir_children_count = 0;
		inodo->i_fop = &assoofs_dir_operations;
	} 
	else if(S_ISREG(mode)){
		printk(KERN_INFO "Petición de nuevo fichero\n");
		inodo_info->file_size = 0;
		inodo->i_fop = &assoofs_file_operations;
	}
	
	aux = assoofs_sb_get_free_block(sb, &inodo_info->data_block_number);
	assoofs_inode_add(sb, inodo_info);

	parent_info = dir-> i_private;
	bH = sb_bread(sb, parent_info->data_block_number);

	record = (struct assoofs_dir_record_entry *)bH->b_data;
	record += parent_info->dir_children_count;
	record->inode_no = inodo_info->inode_no;

	strcpy(record->filename, dentry->d_name.name);
	
	mark_buffer_dirty(bH);
	sync_dirty_buffer(bH);
	brelse(bH);

	parent_info->dir_children_count++;
	aux =assoofs_inode_save(sb,parent_info);;

	inode_init_owner(inodo, dir, mode);
	d_add(dentry, inodo);
	
	return 0;
}

static  struct dentry *assoofs_mount(struct file_system_type *fs_type ,int flags , const char *dev_name , void *data){

	printk(KERN_INFO "MOUNT MODULE\n");

	return mount_bdev(fs_type,flags,dev_name,data,assoofs_fill_super);
}

static struct file_system_type assoofs_type = {
	.owner = THIS_MODULE ,	
	.name = "assoofs",
	.mount = assoofs_mount,
	.kill_sb = kill_litter_super ,
};

static int __init assoofs_init(void){
	int retval;

	retval=register_filesystem(&assoofs_type);	
	
	if(retval==0){
		printk(KERN_INFO "Se ha registrado assoofs correctamente\n");
	}
	else{
		printk(KERN_INFO "La operacion de registro ha fallado: %d\n",retval);
	}
	
   	
    	return retval;
}

static void __exit assoof_exit(void){
	int retval;
	//retval=unregister_filesystem(&assoofs_type);	
	
	if((retval = unregister_filesystem(&assoofs_type))==0){
		printk(KERN_INFO "Modulo eliminado exitosamente\n");
	}
	else{
		printk(KERN_INFO "No se ha eliminado el modulo, ERROR: %d\n",retval);
	}
}

module_init(assoofs_init);
module_exit(assoof_exit);


