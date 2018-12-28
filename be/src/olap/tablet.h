// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#ifndef DORIS_BE_SRC_OLAP_TABLET_H
#define DORIS_BE_SRC_OLAP_TABLET_H

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "gen_cpp/AgentService_types.h"
#include "gen_cpp/olap_file.pb.h"
#include "olap/field.h"
#include "olap/olap_define.h"
#include "olap/tablet_meta.h"
#include "olap/tuple.h"
#include "olap/row_cursor.h"
#include "olap/rowset_graph.h"
#include "olap/utils.h"

namespace doris {
class TabletMeta;
class Rowset;
class Tablet;
class RowBlockPosition;
class DataDir;
class RowsetReader;
class ColumnData;
class SegmentGroup;

using TabletSharedPtr = std::shared_ptr<Tablet>;

struct SchemaChangeStatus {
    SchemaChangeStatus() : status(ALTER_TABLE_WAITING), schema_hash(0), version(-1) {}

    AlterTableStatus status;
    SchemaHash schema_hash;
    int32_t version;
};

class Tablet : public std::enable_shared_from_this<Tablet> {
public:
    static TabletSharedPtr create_from_header_file(
            int64_t tablet_id,
            int64_t schema_hash,
            const std::string& header_file,
            DataDir* data_dir = nullptr);
    static TabletSharedPtr create_from_header(
            TabletMeta* meta,
            DataDir* data_dir  = nullptr);

    Tablet(TabletMeta* tablet_meta, DataDir* data_dir);
    ~Tablet();

    OLAPStatus load();
    bool is_loaded();
    OLAPStatus load_indices();
    OLAPStatus save_tablet_meta();

    OLAPStatus select_versions_to_span(const Version& version,
                                       std::vector<Version>* span_versions) const;
    void acquire_data_sources(const Version& version, std::vector<ColumnData*>* sources) const;
    void acquire_data_sources_by_versions(const std::vector<Version>& version_list,
                                          std::vector<ColumnData*>* sources) const;
    OLAPStatus release_data_sources(std::vector<ColumnData*>* data_sources) const;
    OLAPStatus register_data_source(const std::vector<SegmentGroup*>& segment_group_vec);
    OLAPStatus unregister_data_source(const Version& version, std::vector<SegmentGroup*>* segment_group_vec);
    OLAPStatus add_pending_version(int64_t partition_id, int64_t transaction_id,
                                 const std::vector<std::string>* delete_conditions);
    OLAPStatus add_pending_segment_group(SegmentGroup* segment_group);
    OLAPStatus add_pending_data(SegmentGroup* segment_group, const std::vector<TCondition>* delete_conditions);
    bool has_pending_data(int64_t transaction_id);
    void delete_pending_data(int64_t transaction_id);
    void get_expire_pending_data(std::vector<int64_t>* transaction_ids);
    void delete_expire_incremental_data();
    void load_pending_data();
    OLAPStatus publish_version(int64_t transaction_id, Version version, VersionHash version_hash);
    const PDelta* get_incremental_delta(Version version) const;
    void get_missing_versions_with_header_locked(
            int64_t until_version, std::vector<Version>* missing_versions) const;
    OLAPStatus is_push_for_delete(int64_t transaction_id, bool* is_push_for_delete) const;
    OLAPStatus clone_data(const TabletMeta& clone_header,
                          const std::vector<const PDelta*>& clone_deltas,
                          const std::vector<Version>& versions_to_delete);
    OLAPStatus replace_data_sources(const std::vector<Version>* old_versions,
                                const std::vector<SegmentGroup*>* new_data_sources,
                                std::vector<SegmentGroup*>* old_data_sources);
    OLAPStatus compute_all_versions_hash(const std::vector<Version>& versions,
                                         VersionHash* version_hash) const;
    OLAPStatus merge_tablet_meta(const TabletMeta& hdr, int to_version);
    bool has_version(const Version& version) const;
    void list_versions(std::vector<Version>* versions) const;
    void list_version_entities(std::vector<VersionEntity>* version_entities) const;
    void mark_dropped();
    bool is_dropped();
    void delete_all_files();
    void obtain_header_rdlock() { _meta_lock.rdlock(); }
    void obtain_header_wrlock() { _meta_lock.wrlock(); }
    void release_header_lock() { _meta_lock.unlock(); }
    RWMutex* get_header_lock_ptr() { return &_meta_lock; }
    void obtain_push_lock() { _ingest_lock.lock(); }
    void release_push_lock() { _ingest_lock.unlock(); }
    Mutex* get_push_lock() { return &_ingest_lock; }
    bool try_base_compaction_lock() { return _base_lock.trylock() == OLAP_SUCCESS; }
    void obtain_base_compaction_lock() { _base_lock.lock(); }
    void release_base_compaction_lock() { _base_lock.unlock(); }
    bool try_cumulative_lock() { return (OLAP_SUCCESS == _cumulative_lock.trylock()); }
    void obtain_cumulative_lock() { _cumulative_lock.lock(); }
    void release_cumulative_lock() { _cumulative_lock.unlock(); }
    std::string construct_index_file_path(const Version& version,
                                          VersionHash version_hash,
                                          int32_t segment_group_id, int32_t segment) const;
    std::string construct_data_file_path(const Version& version,
                                         VersionHash version_hash,
                                         int32_t segment_group_id, int32_t segment) const;
    static std::string construct_file_path(const std::string& tablet_path,
                                           const Version& version,
                                           VersionHash version_hash,
                                           int32_t segment_group_id, int32_t segment,
                                           const std::string& suffix);
    std::string construct_pending_data_dir_path() const;
    std::string construct_pending_index_file_path(
        TTransactionId transaction_id, int32_t segment_group_id, int32_t segment) const;
    std::string construct_pending_data_file_path(
        TTransactionId transaction_id, int32_t segment_group_id, int32_t segment) const;
    std::string construct_incremental_delta_dir_path() const;
    std::string construct_incremental_index_file_path(
        Version version, VersionHash version_hash, int32_t segment_group_id, int32_t segment) const;
    std::string construct_incremental_data_file_path(
        Version version, VersionHash version_hash, int32_t segment_group_id, int32_t segment) const;
    std::string construct_dir_path() const;
    std::vector<FieldInfo>& tablet_schema();
    int file_delta_size() const;
    const PDelta& delta(int index) const;
    const PDelta* get_delta(int index) const;
    const PDelta* lastest_delta() const;
    const PDelta* lastest_version() const;
    const PDelta* least_complete_version(
                const std::vector<Version>& missing_versions) const;
    const PDelta* base_version() const;
    const uint32_t get_cumulative_compaction_score() const;
    const uint32_t get_base_compaction_score() const;
    const OLAPStatus delete_version(const Version& version);
    DataFileType data_file_type() const;
    int delete_data_conditions_size() const;
    DeletePredicatePB* add_delete_data_conditions();
    const google::protobuf::RepeatedPtrField<DeletePredicatePB>& delete_data_conditions();
    KeysType keys_type() const;
    bool is_delete_data_version(Version version);
    bool is_load_delete_version(Version version);
    const int64_t creation_time() const;
    void set_creation_time(int64_t time_seconds);
    const int32_t cumulative_layer_point() const;
    void set_cumulative_layer_point(const int32_t new_point);
    bool is_schema_changing();
    bool get_schema_change_request(TTabletId* tablet_id,
                                   TSchemaHash* schema_hash,
                                   std::vector<Version>* versions_to_changed,
                                   AlterTabletType* alter_table_type) const;
    void set_schema_change_request(int64_t tablet_id,
                                   int64_t schema_hash,
                                   const std::vector<Version>& versions_to_changed,
                                   const AlterTabletType alter_table_type);
    bool remove_last_schema_change_version(TabletSharedPtr new_olap_table);
    void clear_schema_change_request();
    SchemaChangeStatus schema_change_status();
    void set_schema_change_status(AlterTableStatus status,
                                  SchemaHash schema_hash,
                                  int32_t version);
    bool equal(int64_t tablet_id, int64_t schema_hash);
    bool is_used();
    std::string storage_root_path_name();
    std::string tablet_path();
    std::string get_field_name_by_index(uint32_t index);
    FieldType get_field_type_by_index(uint32_t index);
    FieldAggregationMethod get_aggregation_by_index(uint32_t index);
    OLAPStatus test_version(const Version& version);
    VersionEntity get_version_entity_by_version(const Version& version);
    size_t get_version_index_size(const Version& version);
    size_t get_version_data_size(const Version& version);
    OLAPStatus recover_tablet_until_specfic_version(const int64_t& until_version,
                                                    const int64_t& version_hash);
    const std::string& rowset_path_prefix();
    void set_id(int64_t id);
    OLAPStatus register_tablet_into_dir();



    OLAPStatus init_once();
    OLAPStatus capture_consistent_rowsets(const Version& spec_version,
                                          vector<std::shared_ptr<RowsetReader>>* rs_readers);
    void acquire_rs_reader_by_version(const vector<Version>& version_vec,
                                      vector<std::shared_ptr<RowsetReader>>* rs_readers) const;
    OLAPStatus release_rs_readers(vector<std::shared_ptr<RowsetReader>>* rs_readers) const;
    OLAPStatus modify_rowsets(vector<RowsetSharedPtr>& to_add,
                             vector<RowsetSharedPtr>& to_delete);

    const int64_t table_id() const;
    const std::string table_name() const;
    const int64_t partition_id() const;
    const int64_t tablet_id() const;
    const int64_t schema_hash() const;
    const int16_t shard_id();
    DataDir* data_dir() const;
    double bloom_filter_fpp() const;
    bool equal(TTabletId tablet_id, TSchemaHash schema_hash);

    TabletSchema* schema() const;
    const std::string& full_name() const;
    size_t num_fields() const;
    size_t num_null_fields();
    size_t num_key_fields();
    size_t num_short_key_fields() const;
    size_t next_unique_id() const;
    size_t num_rows_per_row_block() const;
    CompressKind compress_kind();

    size_t get_field_index(const std::string& field_name) const;
    size_t get_row_size() const;
    size_t get_index_size() const;
    size_t all_rowsets_size() const;
    size_t get_data_size() const;
    size_t get_num_rows() const;
    size_t get_rowset_size(const Version& version);

    AlterTabletState alter_tablet_state();
    TabletState tablet_state() const;

    const RowsetSharedPtr get_rowset(int index) const;
    const RowsetSharedPtr lastest_rowset() const;
    OLAPStatus all_rowsets(vector<RowsetSharedPtr> rowsets);

    OLAPStatus add_inc_rowset(const Rowset& rowset);
    RowsetSharedPtr get_inc_rowset(const Version& version) const;
    OLAPStatus delete_inc_rowset_by_version(const Version& version);
    OLAPStatus delete_expired_inc_rowset();
    OLAPStatus is_deletion_rowset(const Version& version) const;

    OLAPStatus create_snapshot();

    RWMutex* meta_lock();
    Mutex* ingest_lock();
    Mutex* base_lock();
    Mutex* cumulative_lock();

    void calc_missed_versions(int64_t spec_version, vector<Version>* missed_versions) const;

    size_t deletion_rowset_size();
    bool can_do_compaction();

    DeletePredicatePB* add_delete_predicates() {
        return _tablet_meta.add_delete_predicates();
    }

    const google::protobuf::RepeatedPtrField<DeletePredicatePB>&
    delete_predicates();

    google::protobuf::RepeatedPtrField<DeletePredicatePB>*
    mutable_delete_predicate();

    DeletePredicatePB* mutable_delete_predicate(int index);

    OLAPStatus split_range(
            const OlapTuple& start_key_strings,
            const OlapTuple& end_key_strings,
            uint64_t request_block_row_count,
            vector<OlapTuple>* ranges);

    uint32_t segment_size() const;
    void set_io_error();
    RowsetSharedPtr rowset_with_largest_size();
    SegmentGroup* get_largest_index();
public:
    DataDir* _data_dir;
    TabletState _state;
    RowsetGraph* _rs_graph;

    TabletMeta _tablet_meta;
    TabletSchema* _schema;
    RWMutex _meta_lock;
    Mutex _ingest_lock;
    Mutex _base_lock;
    Mutex _cumulative_lock;

    // used for hash-struct of hash_map<Version, Rowset*>.
    struct HashOfVersion {
        size_t operator()(const Version& version) const {
            size_t seed = 0;
            seed = HashUtil::hash64(&version.first, sizeof(version.first), seed);
            seed = HashUtil::hash64(&version.second, sizeof(version.second), seed);
            return seed;
        }
    };
    std::unordered_map<Version, RowsetSharedPtr, HashOfVersion> _rs_version_map;

    DISALLOW_COPY_AND_ASSIGN(Tablet);
};

}  // namespace doris

#endif // DORIS_BE_SRC_OLAP_TABLET_H
