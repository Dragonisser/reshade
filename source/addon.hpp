/*
 * Copyright (C) 2021 Patrick Mours. All rights reserved.
 * License: https://github.com/crosire/reshade#license
 */

#pragma once

#include <string>
#include <vector>
#include <cassert>

namespace reshade::api
{
	template <typename T, typename... api_object_base>
	class api_object_impl : public api_object_base...
	{
	public:
		template <typename... Args>
		explicit api_object_impl(T orig, Args... args) : api_object_base(std::forward<Args>(args)...)..., _orig(orig) {}
		~api_object_impl()
		{
			// All user data should ideally have been removed before destruction, to avoid leaks
			assert(_data_entries.empty());
		}

		api_object_impl(const api_object_impl &) = delete;
		api_object_impl &operator=(const api_object_impl &) = delete;

		bool get_user_data(const uint8_t guid[16], void **ptr) const override
		{
			for (auto it = _data_entries.begin(); it != _data_entries.end(); ++it)
			{
				if (std::memcmp(it->guid, guid, 16) == 0)
				{
					*ptr = it->data;
					return true;
				}
			}
			return false;
		}
		void set_user_data(const uint8_t guid[16], void * const ptr) override
		{
			for (auto it = _data_entries.begin(); it != _data_entries.end(); ++it)
			{
				if (std::memcmp(it->guid, guid, 16) == 0)
				{
					if (ptr == nullptr)
						_data_entries.erase(it);
					else
						it->data = ptr;
					return;
				}
			}

			if (ptr != nullptr)
			{
				_data_entries.push_back({ ptr, {
					reinterpret_cast<const uint64_t *>(guid)[0],
					reinterpret_cast<const uint64_t *>(guid)[1] } });
			}
		}

		uint64_t get_native_object() const override { return (uint64_t)_orig; }

		T _orig;

	private:
		struct entry
		{
			void *data;
			uint64_t guid[2];
		};

		std::vector<entry> _data_entries;
	};
}
