/*
   BAREOS® - Backup Archiving REcovery Open Sourced

   Copyright (C) 2009-2011 Free Software Foundation Europe e.V.
   Copyright (C) 2016-2016 Planets Communications B.V.
   Copyright (C) 2016-2016 Bareos GmbH & Co. KG

   This program is Free Software; you can redistribute it and/or
   modify it under the terms of version three of the GNU Affero General Public
   License as published by the Free Software Foundation and included
   in the file LICENSE.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   Affero General Public License for more details.

   You should have received a copy of the GNU Affero General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.
*/
#ifndef BAREOS_CATS_BDB_MYSQL_H_
#define BAREOS_CATS_BDB_MYSQL_H_ 1

/*
 * Number of insert statements to batch-up in batch insert
 * mode. We use multi-row inserts only in the batch mode
 * on the private database connection.
 */
#define MYSQL_CHANGES_PER_BATCH_INSERT 32

class BareosDbMysql : public BareosDbPrivateInterface {
 private:
  /*
   * Members.
   */
  MYSQL *db_handle_;
  MYSQL instance_;
  MYSQL_RES *result_;
  static const char *query_definitions[]; /**< table of predefined sql queries */

 private:
  /*
   * Methods.
   */
  bool OpenDatabase(JobControlRecord *jcr);
  void CloseDatabase(JobControlRecord *jcr);
  bool ValidateConnection(void);
  void ThreadCleanup(void);
  void EscapeString(JobControlRecord *jcr, char *snew, char *old, int len);
  char *escape_object(JobControlRecord *jcr, char *old, int len);
  void UnescapeObject(JobControlRecord *jcr, char *from, int32_t expected_len, POOLMEM *&dest,
                      int32_t *len);
  void StartTransaction(JobControlRecord *jcr);
  void EndTransaction(JobControlRecord *jcr);
  bool SqlQueryWithHandler(const char *query, DB_RESULT_HANDLER *ResultHandler, void *ctx);
  bool SqlQueryWithoutHandler(const char *query, int flags = 0);
  void SqlFreeResult(void);
  SQL_ROW SqlFetchRow(void);
  const char *sql_strerror(void);
  void SqlDataSeek(int row);
  int SqlAffectedRows(void);
  uint64_t SqlInsertAutokeyRecord(const char *query, const char *table_name);
  SQL_FIELD *SqlFetchField(void);
  bool SqlFieldIsNotNull(int field_type);
  bool SqlFieldIsNumeric(int field_type);
  bool SqlBatchStart(JobControlRecord *jcr);
  bool SqlBatchEnd(JobControlRecord *jcr, const char *error);
  bool SqlBatchInsert(JobControlRecord *jcr, AttributesDbRecord *ar);

 public:
  /*
   * Methods.
   */
  BareosDbMysql(JobControlRecord *jcr, const char *db_driver, const char *db_name,
                const char *db_user, const char *db_password, const char *db_address, int db_port,
                const char *db_socket, bool mult_db_connections, bool disable_batch_insert,
                bool try_reconnect, bool exit_on_fatal, bool need_private);
  ~BareosDbMysql();
};

#endif /* BAREOS_CATS_BDB_MYSQL_H_ */
