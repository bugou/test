/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   copyright            : (C) 2002 by Zhang Yong                         *
 *   email                : z-yong163@163.com                              *
 ***************************************************************************/

#include "icqdb.h"
#include "icqclient.h"

#define INDEX_USER		0
#define INDEX_OPTIONS	0xffffffff
#define INDEX_GROUP		0xfffffffe

string IcqDB::dbDir;


DBOutStream &DBOutStream::operator <<(uint8 b)
{
	if (cursor <= data + MAX_BLOCK_SIZE - sizeof(b))
		*cursor++ = b;
	return (*this);
}

DBOutStream &DBOutStream::operator <<(uint16 w)
{
	if (cursor <= data + MAX_BLOCK_SIZE - sizeof(w)) {
		*(uint16 *) cursor = w;
		cursor += sizeof(w);
	}
	return (*this);
}

DBOutStream &DBOutStream::operator <<(uint32 dw)
{
	if (cursor <= data + MAX_BLOCK_SIZE - sizeof(dw)) {
		*(uint32 *) cursor = dw;
		cursor += sizeof(dw);
	}
	return (*this);
}

DBOutStream &DBOutStream::operator <<(const char *str)
{
	uint16 len = strlen(str) + 1;
	if (cursor <= data + MAX_BLOCK_SIZE - sizeof(len) - len) {
		*this << len;
		memcpy(cursor, str, len);
		cursor += len;
	}
	return (*this);
}

DBOutStream &DBOutStream::operator <<(StrList &strList)
{
	uint16 n = 0;
	char *old = cursor;
	cursor += sizeof(n);
	
	StrList::iterator i;
	for (i = strList.begin(); i != strList.end(); i++) {
		operator <<(*i);
		n++;
	}
	char *p = cursor;
	cursor = old;
	operator <<(n);
	cursor = p;
	return (*this);
}

DBInStream &DBInStream::operator >>(uint8 &b)
{
	if (cursor <= data + datalen - sizeof(b))
		b = *cursor++;
	else
		b = 0;
	return (*this);
}

DBInStream &DBInStream::operator >>(uint16 &w)
{
	if (cursor <= data + datalen - sizeof(w)) {
		w = *(uint16 *) cursor;
		cursor += sizeof(w);
	} else
		w = 0;
	return (*this);
}

DBInStream &DBInStream::operator >>(uint32 &dw)
{
	if (cursor <= data + datalen - sizeof(dw)) {
		dw = *(uint32 *) cursor;
		cursor += sizeof(dw);
	} else
		dw = 0;
	return (*this);
}

DBInStream &DBInStream::operator >>(string &str)
{
	uint16 len;
	operator >>(len);

	if (cursor <= data + datalen - len && !cursor[len - 1]) {
		str = cursor;
		cursor += len;
	} else
		str = "";
	return (*this);
}

DBInStream &DBInStream::operator >>(StrList &strList)
{
	uint16 num;
	operator >>(num);
	int n = (int) num;

	strList.clear();
	while (n-- > 0) {
		string s;
		operator >>(s);
		strList.push_back(s);
	}
	return (*this);
}


static const char userFile[] = "user.db";
static const char msgFile[] = "msg.db";
static const char quickReplyFile[] = "quickreply.txt";
static const char autoReplyFile[] = "autoreply.txt";

DB *IcqDB::getDBFullPath(const char *pathName, bool dup)
{
	DB *db;
	if (db_create(&db, NULL, 0) != 0)
		return NULL;

	if (dup && db->set_flags(db, DB_DUP) != 0) {
		db->close(db, 0);
		return NULL;
	}
	if (db->open(db, pathName, NULL, DB_HASH, DB_CREATE, 0600) != 0) {
		db->close(db, 0);
		return NULL;
	}
	return db;
}

bool IcqDB::saveBlock(DB *db, uint32 index, DBSerialize &obj)
{
	DBOutStream out;
	obj.save(out);

	DBT key, data;
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	key.data = &index;
	key.size = sizeof(index);
	data.data = out.getData();
	data.size = out.getSize();

	return (db->put(db, NULL, &key, &data, 0) == 0);
}

bool IcqDB::saveBlock(const char *fileName, uint32 index, DBSerialize &obj, bool dup)
{
	DB *db = getDB(fileName, dup);
	if (!db)
		return false;

	int ret = saveBlock(db, index, obj);
	db->close(db, 0);
	return (ret == 0);
}

bool IcqDB::loadBlock(const char *fileName, uint32 index, DBSerialize &obj)
{
	DB *db = getDB(fileName, false);
	if (!db)
		return false;

	DBT key, data;
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	key.data = &index;
	key.size = sizeof(index);

	if (db->get(db, NULL, &key, &data, 0) != 0) {
		db->close(db, 0);
		return false;
	}

	DBInStream in(data.data, data.size);
	obj.load(in);

	db->close(db, 0);
	return true;
}

bool IcqDB::delIndex(const char *fileName, uint32 index)
{
	DB *db = getDB(fileName);
	if (!db)
		return false;

	DBT key;
	memset(&key, 0, sizeof(key));
	key.data = &index;
	key.size = sizeof(index);
	int ret = db->del(db, NULL, &key, 0);

	db->close(db, 0);
	return (ret == 0);
}

bool IcqDB::saveMsg(IcqMsg &msg)
{
	uint32 uin = (msg.isSysMsg() ? 0 : msg.uin);
	return saveBlock(msgFile, uin, msg, true);
}

bool IcqDB::delMsg(uint32 uin)
{
	return delIndex(msgFile, uin);
}

bool IcqDB::delMsg(uint32 uin, int item)
{
	DB *db = getDB(msgFile);
	if (!db)
		return false;

	DBC *cursor;
	if (db->cursor(db, NULL, &cursor, 0) != 0) {
		db->close(db, 0);
		return false;
	}

	DBT key, data;
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	key.data = &uin;
	key.size = sizeof(uin);
	if (cursor->c_get(cursor, &key, &data, DB_SET) != 0) {
		db->close(db, 0);
		return false;
	}

	for (int i = 0; i < item; ++i) {
		if (cursor->c_get(cursor, &key, &data, DB_NEXT_DUP) != 0)
			break;
	}
	if (i < item) {
		cursor->c_close(cursor);
		db->close(db, 0);
		return false;
	}

	int ret = cursor->c_del(cursor, 0);

	cursor->c_close(cursor);
	db->close(db, 0);
	return (ret == 0);
}

bool IcqDB::loadMsg(uint32 uin, PtrList &msgList)
{
	DB *db = getDB(msgFile, true);
	if (!db)
		return false;

	DBC *cursor;
	if (db->cursor(db, NULL, &cursor, 0) != 0) {
		db->close(db, 0);
		return false;
	}

	DBT key, data;
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	key.data = &uin;
	key.size = sizeof(uin);

	if (cursor->c_get(cursor, &key, &data, DB_SET) != 0) {
		cursor->c_close(cursor);
		db->close(db, 0);
		return false;
	}

	do {
		DBInStream in(data.data, data.size);
		IcqMsg *msg = new IcqMsg;
		msg->load(in);
		msgList.push_back(msg);

	} while (cursor->c_get(cursor, &key, &data, DB_NEXT_DUP) == 0);

	cursor->c_close(cursor);
	db->close(db, 0);
	return true;
}

bool IcqDB::saveContact(IcqContact &c)
{
	return saveBlock(userFile, c.uin, c, false);
}

bool IcqDB::loadContact(IcqContact &c)
{
	return loadBlock(userFile, c.uin, c);
}

bool IcqDB::loadContact(PtrList &contactList)
{
	DB *db = getDB(userFile);
	if (!db)
		return false;

	DBC *cursor;
	if (db->cursor(db, NULL, &cursor, 0) != 0) {
		db->close(db, 0);
		return false;
	}

	DBT key, data;
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));

	while (cursor->c_get(cursor, &key, &data, DB_NEXT) == 0) {
		uint32 uin = *(uint32 *) key.data;
		if (uin > 0 && uin <= 0xfffffff0) {
			DBInStream in(data.data, data.size);
			IcqContact *c = new IcqContact;
			c->load(in);
			contactList.push_back(c);
		}
	}

	cursor->c_close(cursor);
	db->close(db, 0);
	return true;
}

bool IcqDB::delContact(uint32 uin)
{
	return delIndex(userFile, uin);
}

bool IcqDB::saveUser(IcqUser &user)
{
	return saveBlock(userFile, INDEX_USER, user, false);
}

bool IcqDB::loadUser(IcqUser &user)
{
	return loadBlock(userFile, INDEX_USER, user);
}

bool IcqDB::saveOptions(IcqOption &options)
{
	return saveBlock(userFile, INDEX_OPTIONS, options, false);
}

bool IcqDB::loadOptions(IcqOption &options)
{
	return loadBlock(userFile, INDEX_OPTIONS, options);
}

bool IcqDB::saveGroupInfo(DBSerialize &obj)
{
	return saveBlock(userFile, INDEX_GROUP, obj, false);
}

bool IcqDB::loadGroupInfo(DBSerialize &obj)
{
	return loadBlock(userFile, INDEX_GROUP, obj);
}

bool IcqDB::exportMsg(const char *pathName, uint32 uin)
{
	PtrList msgList;
	if (!loadMsg(uin, msgList))
		return false;

	DB *db = getDBFullPath(pathName, true);
	if (!db)
		return false;

	bool cont = true;
	while (!msgList.empty() && cont) {
		IcqMsg *msg = (IcqMsg *) msgList.front();
		msgList.pop_front();

		cont = saveBlock(db, uin, *msg);
		delete msg;
	}

	db->close(db, 0);
	return cont;
}

bool IcqDB::importRecord(const char *pathName)
{
	bool ret = false;
	DB *db = getDBFullPath(pathName, true);
	DB *dbOut = getDB(msgFile, true);
	if (!db || !dbOut)
		goto import_exit;

	DBC *cursor;
	if (db->cursor(db, NULL, &cursor, 0) != 0)
		goto import_exit;

	DBT key, data;
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));

	ret = true;
	while (cursor->c_get(cursor, &key, &data, DB_NEXT) == 0 && ret)
		ret = (dbOut->put(dbOut, NULL, &key, &data, 0) == 0);

import_exit:
	if (db)
		db->close(db, 0);
	if (dbOut)
		dbOut->close(dbOut, 0);
	return ret;
}

bool IcqDB::saveConfig(const char *fileName, DBSerialize &obj)
{
	string pathName = dbDir + fileName;
	FILE *file = fopen(pathName.c_str(), "wb");
	if (!file)
		return false;

	DBOutStream out;
	obj.save(out);
	fwrite(out.getData(), out.getSize(), 1, file);

	fclose(file);
	return true;
}

bool IcqDB::loadConfig(const char *fileName, DBSerialize &obj)
{
	string pathName = dbDir + fileName;
	FILE *file = fopen(pathName.c_str(), "rb");
	if (!file)
		return false;

	char buf[4096];
	int n = fread(buf, 1, sizeof(buf), file);
	fclose(file);

	DBInStream in(buf, n);
	obj.load(in);
	return true;
}

bool IcqDB::getMsgUinList(UinList &uinList)
{
	DB *db = getDB(msgFile, true);
	if (!db)
		return false;

	DBC *cursor;
	if (db->cursor(db, NULL, &cursor, 0) != 0) {
		db->close(db, 0);
		return false;
	}

	DBT key, data;
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));

	while (cursor->c_get(cursor, &key, &data, DB_NEXT_NODUP) == 0)
		uinList.push_back(*(uint32 *) key.data);

	return true;
}

void IcqDB::loadStrList(const char *fileName, StrList &l)
{
	string pathName = dbDir + fileName;
	FILE *file = fopen(pathName.c_str(), "r");
	
	if (file) {
		char line[128];
		while (fgets(line, sizeof(line), file)) {
			int len = strlen(line);
			if (len > 0) {
				if (line[len - 1] == '\n')
					line[len - 1] = '\0';
				l.push_back(line);
			}
		}
		fclose(file);
	}
}

void IcqDB::saveStrList(const char *fileName, StrList &l)
{
	string pathName = dbDir + fileName;
	FILE *file = fopen(pathName.c_str(), "w");
	
	if (file) {
		StrList::iterator it;
		for (it = l.begin(); it != l.end(); ++it)
			fprintf(file, "%s\n", (*it).c_str());
		fclose(file);
	}
}

void IcqDB::loadQuickReply(StrList &l) {
	loadStrList(quickReplyFile, l);
}

void IcqDB::saveQuickReply(StrList &l) {
	saveStrList(quickReplyFile, l);
}

void IcqDB::loadAutoReply(StrList &l) {
	loadStrList(autoReplyFile, l);
}

void IcqDB::saveAutoReply(StrList &l) {
	saveStrList(autoReplyFile, l);
}
