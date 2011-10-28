#include "restore_emitter.h"

using namespace boost;
using namespace std;
using namespace thin_provisioning;

//----------------------------------------------------------------

namespace {
	class restorer : public emitter {
	public:
		restorer(metadata::ptr md)
			: md_(md),
			  in_superblock_(false) {
		}

		virtual ~restorer() {
			if (in_superblock_) {
				throw runtime_error("still in superblock");
			}
		}

		virtual void begin_superblock(std::string const &uuid,
					      uint64_t time,
					      uint64_t trans_id,
					      uint32_t data_block_size) {
			in_superblock_ = true;

			superblock &sb = md_->sb_;
			memcpy(&sb.uuid_, &uuid, sizeof(&sb.uuid_));
			sb.time_ = time;
			sb.trans_id_ = trans_id;
			sb.data_block_size_ = data_block_size;
		}

		virtual void end_superblock() {
			if (!in_superblock_)
				throw runtime_error("missing superblock");

			md_->commit();

			in_superblock_ = false;
		}

		virtual void begin_device(uint32_t dev,
					  uint64_t mapped_blocks,
					  uint64_t trans_id,
					  uint64_t creation_time,
					  uint64_t snap_time) {
			if (!in_superblock_)
				throw runtime_error("missing superblock");

			if (device_exists(dev))
				throw std::runtime_error("Device already exists");

			// Add entry to the details tree
			uint64_t key[1] = {dev};
			device_details details = {mapped_blocks, trans_id, creation_time, snap_time};
			md_->details_.insert(key, details);

			// Insert an empty mapping tree
			single_mapping_tree::ptr new_tree(
				new single_mapping_tree(md_->tm_,
							block_time_ref_counter(md_->data_sm_)));
			md_->mappings_top_level_.insert(key, new_tree->get_root());
			md_->mappings_.set_root(md_->mappings_top_level_.get_root()); // FIXME: ugly

			current_device_ = optional<uint32_t>(dev);
		}

		virtual void end_device() {
			current_device_ = optional<uint32_t>();
		}

		virtual void begin_named_mapping(std::string const &name) {
			throw runtime_error("not implemented");
		}

		virtual void end_named_mapping() {
			throw runtime_error("not implemented");
		}

		virtual void identifier(std::string const &name) {
			throw runtime_error("not implemented");
		}

		virtual void range_map(uint64_t origin_begin, uint64_t data_begin, uint32_t time, uint64_t len) {
			for (uint64_t i = 0; i < len; i++)
				single_map(origin_begin++, data_begin++, time);
		}

		virtual void single_map(uint64_t origin_block, uint64_t data_block, uint32_t time) {
			if (!current_device_)
				throw runtime_error("not in device");

			uint64_t key[2] = {*current_device_, origin_block};
			block_time bt;
			bt.block_ = data_block;
			bt.time_ = time;
			md_->mappings_.insert(key, bt);
			md_->mappings_top_level_.set_root(md_->mappings_.get_root());
		}

	private:
		bool device_exists(thin_dev_t dev) const {
			uint64_t key[1] = {dev};
			return md_->details_.lookup(key);
		}

		metadata::ptr md_;
		bool in_superblock_;
		optional<uint32_t> current_device_;
	};
}

//----------------------------------------------------------------

emitter::ptr
thin_provisioning::create_restore_emitter(metadata::ptr md)
{
	return emitter::ptr(new restorer(md));
}

//----------------------------------------------------------------