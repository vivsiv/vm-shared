#include "BTreeNode.h"

using namespace std;

/*
 * Read the content of the node from the page pid in the PageFile pf.
 * @param pid[IN] the PageId to read
 * @param pf[IN] PageFile to read from
 * @return 0 if successful. Return an error code if there is an error.
 */
void BTLeafNode::printNode(){
	printf("Printing Node of Size: %d\n", keyCount);
	LeafNodeEntry *currentEntry = getFirstEntry();
	for (int i=0; i < keyCount; i++){
		printf("Printing entry at addr %p\n", (void *)currentEntry);
		printf("Entry, key: %d pageId:%d slotId: %d\n", currentEntry->key, (currentEntry->recordId).pid, (currentEntry->recordId).sid);
		currentEntry++;
	}
	printf("\n");
}

BTLeafNode::NodeHead* BTLeafNode::getHead(){
	return reinterpret_cast<NodeHead*>(buffer);
}

BTLeafNode::NodeTail* BTLeafNode::getTail(){
	char* tailAddr = buffer + PageFile::PAGE_SIZE - sizeof(NodeTail);
	return reinterpret_cast<NodeTail*>(tailAddr);
}

void BTLeafNode::setKeyCount(int k){
	keyCount = k;
	NodeHead *nh = getHead();
	nh->size = k;
	NodeTail *nt = getTail();
	nt->size = k;
}

BTLeafNode::LeafNodeEntry* BTLeafNode::getFirstEntry(){
	char* startAddr = buffer + sizeof(NodeHead);
	return reinterpret_cast<LeafNodeEntry *>(startAddr);
}

BTLeafNode::LeafNodeEntry* BTLeafNode::getLastEntry(){
	char* startAddr = buffer + sizeof(NodeHead);
	if (keyCount > 0) startAddr += keyCount  * (sizeof(LeafNodeEntry));
	return reinterpret_cast<LeafNodeEntry *>(startAddr);
}

BTLeafNode::LeafNodeEntry* BTLeafNode::getMiddleEntry(){
	int middle = (keyCount / 2) + (keyCount % 2);
	char* startAddr = buffer + sizeof(NodeHead) + (middle * (sizeof(LeafNodeEntry)));
	return reinterpret_cast<LeafNodeEntry *>(startAddr);
}

RC BTLeafNode::read(PageId pid, const PageFile& pf)
{
	RC errcode;
	errcode = pf.read(pid, buffer);
	pageId = pid;
	NodeHead *nh = getHead();
	NodeTail *nt = getTail();
	if (nh->size == nt->size) setKeyCount(nh->size);
	else setKeyCount(0);

	return errcode;
}
    
/*
 * Write the content of the node to the page pid in the PageFile pf.
 * @param pid[IN] the PageId to write to
 * @param pf[IN] PageFile to write to
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::write(PageId pid, PageFile& pf)
{ 
	return pf.write(pid,buffer);
}

/*
 * Return the number of keys stored in the node.
 * @return the number of keys in the node
 */
int BTLeafNode::getKeyCount()
{
	return keyCount;
}

/*
 * Insert a (key, rid) pair to the node.
 * @param key[IN] the key to insert
 * @param rid[IN] the RecordId to insert
 * @return 0 if successful. Return an error code if the node is full.
 */
RC BTLeafNode::insert(int key, const RecordId& rid)
{ 
	LeafNodeEntry *currentEntry = getFirstEntry();
	int currentKey = 0;
	while(currentKey < keyCount && currentEntry->key < key){
		currentEntry++;
		currentKey++;
	}
	LeafNodeEntry *newEntry = currentEntry;
	memmove(currentEntry + 1, currentEntry, (keyCount - currentKey) * sizeof(LeafNodeEntry));
	newEntry->recordId = rid;
	newEntry->key = key;
	//keyCount++;
	setKeyCount(keyCount + 1);
	return 0; 
}

/*
 * Insert the (key, rid) pair to the node
 * and split the node half and half with sibling.
 * The first key of the sibling node is returned in siblingKey.
 * @param key[IN] the key to insert.
 * @param rid[IN] the RecordId to insert.
 * @param sibling[IN] the sibling node to split with. This node MUST be EMPTY when this function is called.
 * @param siblingKey[OUT] the first key in the sibling node after split.
 * @return 0 if successful. Return an error code if there is an error.
 */

void BTLeafNode::initialize(int numKeys, int prev, const char* cpyStart){
	LeafNodeEntry* firstEntry = getFirstEntry();
	memcpy(firstEntry, cpyStart, numKeys * sizeof(LeafNodeEntry));
	setKeyCount(numKeys);
	NodeHead *nh = getHead();
	nh->prevPage = prev;
}

RC BTLeafNode::insertAndSplit(int key, const RecordId& rid, 
                              BTLeafNode& sibling, int& siblingKey)
{
	insert(key,rid);
	LeafNodeEntry* splitPoint = getMiddleEntry();

	int leftSize = keyCount - (keyCount/2);
	int rightSize = keyCount/2;

	sibling.initialize(rightSize, pageId, reinterpret_cast<char*>(splitPoint));
	siblingKey = (sibling.getFirstEntry())->key;

	memset(splitPoint, 0, rightSize * sizeof(LeafNodeEntry));
	setKeyCount(leftSize);

	return 0; 
}

/**
 * If searchKey exists in the node, set eid to the index entry
 * with searchKey and return 0. If not, set eid to the index entry
 * immediately after the largest index key that is smaller than searchKey,
 * and return the error code RC_NO_SUCH_RECORD.
 * Remember that keys inside a B+tree node are always kept sorted.
 * @param searchKey[IN] the key to search for.
 * @param eid[OUT] the index entry number with searchKey or immediately
                   behind the largest key smaller than searchKey.
 * @return 0 if searchKey is found. Otherwise return an error code.
 */
RC BTLeafNode::locate(int searchKey, int& eid)
{ 
	int currKey, keyIdx;
	RC errcode;
	LeafNodeEntry *currentEntry = getFirstEntry();
	keyIdx = 0;
	errcode = 0;
	while (keyIdx < keyCount){
		currKey = currentEntry->key;
		if (currKey == searchKey){
			eid = keyIdx;
			break;
		}
		else if (currKey > searchKey){
			//if (keyIdx > 0) currentEntry--;
			eid = keyIdx;
			errcode = RC_NO_SUCH_RECORD;
			break;
		}
		currentEntry++;
		keyIdx++;
	}
	return errcode; 
}

/*
 * Read the (key, rid) pair from the eid entry.
 * @param eid[IN] the entry number to read the (key, rid) pair from
 * @param key[OUT] the key from the entry
 * @param rid[OUT] the RecordId from the entry
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::readEntry(int eid, int& key, RecordId& rid)
{ 
	RC errcode;
	if (eid >= 0 && eid < keyCount){
		LeafNodeEntry *firstEntry = getFirstEntry();
		LeafNodeEntry *readEntry = firstEntry + eid;
		key = readEntry->key;
		rid = readEntry->recordId;
		errcode = 0;
	}
	else {
		errcode = RC_NO_SUCH_RECORD;
	}
	return errcode; 
}

/*
 * Return the pid of the next slibling node.
 * @return the PageId of the next sibling node 
 */
PageId BTLeafNode::getNextNodePtr()
{ 
	NodeTail* nt = getTail();
	return nt->nextPage;
}

/*
 * Set the pid of the next slibling node.
 * @param pid[IN] the PageId of the next sibling node 
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::setNextNodePtr(PageId pid)
{ 
	NodeTail* nt = getTail();
	nt->nextPage = pid;	
	return 0; 
}

/*
 * Read the content of the node from the page pid in the PageFile pf.
 * @param pid[IN] the PageId to read
 * @param pf[IN] PageFile to read from
 * @return 0 if successful. Return an error code if there is an error.
 */
void BTNonLeafNode::printNode(){
	printf("Printing Node of Size: %d\n", keyCount);
	NonLeafNodeEntry *currentEntry = getFirstEntry();
	for (int i=0; i < keyCount; i++){
		printf("Printing entry at addr %p\n", (void *)currentEntry);
		printf("Entry, pageId:%d keyId:%d\n", currentEntry->pageId, currentEntry->key);
		currentEntry++;
	}
	printf("\n");	
}

int BTNonLeafNode::checkSize(){
	int *firstSize = reinterpret_cast<int*>(buffer);
	int *lastSize = reinterpret_cast<int*>(buffer + PageFile::PAGE_SIZE - sizeof(int));
	return *firstSize == *lastSize;
}

void BTNonLeafNode::setKeyCount(int k){
	keyCount = k;
	int *firstSize = reinterpret_cast<int*>(buffer);
	*firstSize = k;
	int *lastSize = reinterpret_cast<int*>(buffer + PageFile::PAGE_SIZE - sizeof(int));
	*lastSize = k;
}

BTNonLeafNode::NonLeafNodeEntry* BTNonLeafNode::getFirstEntry(){
	char* startAddr = buffer + sizeof(int);
	return reinterpret_cast<NonLeafNodeEntry *>(startAddr);
}

BTNonLeafNode::NonLeafNodeEntry* BTNonLeafNode::getMiddleEntry(){
	int middle = (keyCount / 2);
	char* startAddr = buffer + sizeof(int) + (middle *(sizeof(NonLeafNodeEntry)));
	printf("Midpoint address:%p\n",startAddr);
	return reinterpret_cast<NonLeafNodeEntry *>(startAddr);

}

RC BTNonLeafNode::read(PageId pid, const PageFile& pf)
{ 
	RC errcode;
	errcode = pf.read(pid, buffer);
	pageId = pid;
	if (checkSize()) setKeyCount(*(reinterpret_cast<int*>(buffer)));
	else setKeyCount(0);

	return errcode;
}
    
/*
 * Write the content of the node to the page pid in the PageFile pf.
 * @param pid[IN] the PageId to write to
 * @param pf[IN] PageFile to write to
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTNonLeafNode::write(PageId pid, PageFile& pf)
{ 
	return pf.write(pid,buffer);
}

/*
 * Return the number of keys stored in the node.
 * @return the number of keys in the node
 */
int BTNonLeafNode::getKeyCount()
{ 
	return keyCount; 
}


/*
 * Insert a (key, pid) pair to the node.
 * @param key[IN] the key to insert
 * @param pid[IN] the PageId to insert
 * @return 0 if successful. Return an error code if the node is full.
 */
RC BTNonLeafNode::insert(int key, PageId pid)
{
	NonLeafNodeEntry* currentEntry = getFirstEntry();
	int currentKey = 0;
	while(currentKey < keyCount && currentEntry->key < key){
		currentEntry++;
		currentKey++;
	}
	NonLeafNodeEntry *newEntry = currentEntry;
	memmove(currentEntry + 1, currentEntry, (keyCount - currentKey) * sizeof(NonLeafNodeEntry));
	newEntry->pageId = pid;
	newEntry->key = key;
	setKeyCount(keyCount + 1);
	return 0; 
}

/*
 * Insert the (key, pid) pair to the node
 * and split the node half and half with sibling.
 * The middle key after the split is returned in midKey.
 * @param key[IN] the key to insert
 * @param pid[IN] the PageId to insert
 * @param sibling[IN] the sibling node to split with. This node MUST be empty when this function is called.
 * @param midKey[OUT] the key in the middle after the split. This key should be inserted to the parent node.
 * @return 0 if successful. Return an error code if there is an error.
 */

void BTNonLeafNode::initialize(int numKeys, const char* cpyStart){
	NonLeafNodeEntry* firstEntry = getFirstEntry();
	memcpy(firstEntry, cpyStart, numKeys * sizeof(NonLeafNodeEntry));
	setKeyCount(numKeys);
}

RC BTNonLeafNode::insertAndSplit(int key, PageId pid, BTNonLeafNode& sibling, int& midKey)
{ 
	insert(key,pid);
	printf("Before splitting\n");
	printNode();
	printf("---\n");
	NonLeafNodeEntry *midPoint = getMiddleEntry();
	midKey = midPoint->key;
	printf("Pre Split MidKey:%d\n",midKey);

	NonLeafNodeEntry *splitPoint = midPoint + 1;
	int leftSize = keyCount/2;
	int rightSize = keyCount/2 - !(keyCount % 2);

	sibling.initialize(rightSize, reinterpret_cast<char*>(splitPoint));
	
	memset(midPoint, 0, (rightSize + 1) * sizeof(NonLeafNodeEntry));
	setKeyCount(leftSize);

	return 0; 
}

/*
 * Given the searchKey, find the child-node pointer to follow and
 * output it in pid.
 * @param searchKey[IN] the searchKey that is being looked up.
 * @param pid[OUT] the pointer to the child node to follow.
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTNonLeafNode::locateChildPtr(int searchKey, PageId& pid)
{ 
	RC errcode;
	NonLeafNodeEntry* currentEntry = getFirstEntry();
	for (int i; i < keyCount; i++){
		if (searchKey == currentEntry->key){
			pid = currentEntry->pageId;
			errcode = 0;
			break;
		}
		else errcode = RC_NO_SUCH_RECORD;
	}
	return errcode; 
}

/*
 * Initialize the root node with (pid1, key, pid2).
 * @param pid1[IN] the first PageId to insert
 * @param key[IN] the key that should be inserted between the two PageIds
 * @param pid2[IN] the PageId to insert behind the key
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTNonLeafNode::initializeRoot(PageId pid1, int key, PageId pid2)
{ 
	PageFile pf;
	BTNonLeafNode root;
	root.insert(key, pid1);
	root.insert(NO_KEY,pid2);

	return 0; 
}
