/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#pragma once
#include <graphene/chain/protocol/asset_ops.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <graphene/db/flat_index.hpp>
#include <graphene/db/generic_index.hpp>

/**
 * @defgroup prediction_market Prediction Market
 *
 * A prediction market is a specialized BitAsset such that total debt and total collateral are always equal amounts
 * (although asset IDs differ). No margin calls or force settlements may be performed on a prediction market asset. A
 * prediction market is globally settled by the issuer after the event being predicted resolves, thus a prediction
 * market must always have the @ref global_settle permission enabled. The maximum price for global settlement or short
 * sale of a prediction market asset is 1-to-1.
 */

namespace graphene { namespace chain {
   class account_object;
   class database;
   using namespace graphene::db;

   /**
    *  @brief tracks the asset information that changes frequently
    *  @ingroup object
    *  @ingroup implementation
    *
    *  Because the asset_object is very large it doesn't make sense to save an undo state
    *  for all of the parameters that never change.   This object factors out the parameters
    *  of an asset that change in almost every transaction that involves the asset.
    *
    *  This object exists as an implementation detail and its ID should never be referenced by
    *  a blockchain operation.
    */
   class asset_dynamic_data_object : public abstract_object<asset_dynamic_data_object>
   {
      public:
         static const uint8_t space_id = implementation_ids;
         static const uint8_t type_id  = impl_asset_dynamic_data_type;

         /// The number of shares currently in existence
		 share_type guard_lock_balance = 0;
         share_type current_supply;
         share_type confidential_supply; ///< total asset held in confidential balances
         share_type accumulated_fees; ///< fees accumulate to be paid out over time
         share_type fee_pool;         ///< in core asset
		 share_type withdraw_limition;
		 std::string gas_price = "5000000000";
   };

   /**
    *  @brief tracks the parameters of an asset
    *  @ingroup object
    *
    *  All assets have a globally unique symbol name that controls how they are traded and an issuer who
    *  has authority over the parameters of the asset.
    */
   class asset_object : public graphene::db::abstract_object<asset_object>
   {
      public:
         static const uint8_t space_id = protocol_ids;
         static const uint8_t type_id  = asset_object_type;

         /// This function does not check if any registered asset has this symbol or not; it simply checks whether the
         /// symbol would be valid.
         /// @return true if symbol is a valid ticker symbol; false otherwise.
         static bool is_valid_symbol( const string& symbol );

         /// @return true if this is a market-issued asset; false otherwise.
         bool is_market_issued()const { return bitasset_data_id.valid(); }
         /// @return true if users may request force-settlement of this market-issued asset; false otherwise
         bool can_force_settle()const { return !(options.flags & disable_force_settle); }
         /// @return true if the issuer of this market-issued asset may globally settle the asset; false otherwise
         bool can_global_settle()const { return options.issuer_permissions & global_settle; }
         /// @return true if this asset charges a fee for the issuer on market operations; false otherwise
         bool charges_market_fees()const { return options.flags & charge_market_fee; }
         /// @return true if this asset may only be transferred to/from the issuer or market orders
         bool is_transfer_restricted()const { return options.flags & transfer_restricted; }
         bool can_override()const { return options.flags & override_authority; }
         bool allow_confidential()const { return !(options.flags & asset_issuer_permission_flags::disable_confidential); }

         /// Helper function to get an asset object with the given amount in this asset's type
         asset amount(share_type a)const { return asset(a, id); }
         /// Convert a string amount (i.e. "123.45") to an asset object with this asset's type
         /// The string may have a decimal and/or a negative sign.
         asset amount_from_string(string amount_string)const;
         /// Convert an asset to a textual representation, i.e. "123.45"
         string amount_to_string(share_type amount)const;
         /// Convert an asset to a textual representation, i.e. "123.45"
         string amount_to_string(const asset& amount)const
         { FC_ASSERT(amount.asset_id == id); return amount_to_string(amount.amount); }
         /// Convert an asset to a textual representation with symbol, i.e. "123.45 USD"
         string amount_to_pretty_string(share_type amount)const
         { return amount_to_string(amount) + " " + symbol; }
         /// Convert an asset to a textual representation with symbol, i.e. "123.45 USD"
         string amount_to_pretty_string(const asset &amount)const
         { FC_ASSERT(amount.asset_id == id); return amount_to_pretty_string(amount.amount); }

         /// Ticker symbol for this asset, i.e. "USD"
         string symbol;
         /// Maximum number of digits after the decimal point (must be <= 12)
         uint8_t precision = 0;
         /// ID of the account which issued this asset.
         account_id_type issuer;

         asset_options options;

		 enum public_asset_type
		 {
			 normal_type = 0,
			 crosschain_type = 1
		 };

		 public_asset_type asset_type = normal_type;


         /// Current supply, fee pool, and collected fees are stored in a separate object as they change frequently.
         asset_dynamic_data_id_type  dynamic_asset_data_id;
         /// Extra data associated with BitAssets. This field is non-null if and only if is_market_issued() returns true
         optional<asset_bitasset_data_id_type> bitasset_data_id;

         optional<account_id_type> buyback_account;

		 /// Feeds published for this asset. If issuer is not committee, the keys in this map are the feed publishing
		 /// accounts; otherwise, the feed publishers are the currently active committee_members and witnesses and this map
		 /// should be treated as an implementation detail. The timestamp on each feed is the time it was published.
		 flat_set<address> publishers; 
		 flat_map<address, pair<time_point_sec, price_feed>> feeds;
		 /// This is the currently active price feed, calculated as the median of values from the currently active
		 /// feeds.
		 price_feed current_feed;

		 time_point_sec current_feed_publication_time;
		 bool allow_withdraw_deposit=true;
         asset_id_type get_id()const { return id; }

         void validate()const
         {
            // UIAs may not be prediction markets, have force settlement, or global settlements
            if( !is_market_issued() )
            {
               FC_ASSERT(!(options.flags & disable_force_settle || options.flags & global_settle));
               FC_ASSERT(!(options.issuer_permissions & disable_force_settle || options.issuer_permissions & global_settle));
            }
         }

         template<class DB>
         const asset_bitasset_data_object& bitasset_data(const DB& db)const
         { assert(bitasset_data_id); return db.get(*bitasset_data_id); }

         template<class DB>
         const asset_dynamic_data_object& dynamic_data(const DB& db)const
         { return db.get(dynamic_asset_data_id); }

         /**
          *  The total amount of an asset that is reserved for future issuance. 
          */
         template<class DB>
         share_type reserved( const DB& db )const
         { return options.max_supply - dynamic_data(db).current_supply; }


		 time_point_sec feed_expiration_time()const
		 {
			 return current_feed_publication_time + GRAPHENE_DEFAULT_PRICE_FEED_LIFETIME*30;
		 }
		 bool feed_is_expired(time_point_sec current_time)const
		 {
			 return feed_expiration_time() <= current_time;
		 }
		 void update_median_feeds(time_point_sec current_time);
   };

   /**
    *  @brief contains properties that only apply to bitassets (market issued assets)
    *
    *  @ingroup object
    *  @ingroup implementation
    */
   class asset_bitasset_data_object : public abstract_object<asset_bitasset_data_object>
   {
      public:
         static const uint8_t space_id = implementation_ids;
         static const uint8_t type_id  = impl_asset_bitasset_data_type;

         /// The tunable options for BitAssets are stored in this field.
         bitasset_options options;

         /// Feeds published for this asset. If issuer is not committee, the keys in this map are the feed publishing
         /// accounts; otherwise, the feed publishers are the currently active committee_members and witnesses and this map
         /// should be treated as an implementation detail. The timestamp on each feed is the time it was published.
         flat_map<account_id_type, pair<time_point_sec,price_feed>> feeds;
         /// This is the currently active price feed, calculated as the median of values from the currently active
         /// feeds.
         price_feed current_feed;
         /// This is the publication time of the oldest feed which was factored into current_feed.
         time_point_sec current_feed_publication_time;

         /// True if this asset implements a @ref prediction_market
         bool is_prediction_market = false;

         /// This is the volume of this asset which has been force-settled this maintanence interval
         share_type force_settled_volume;
         /// Calculate the maximum force settlement volume per maintenance interval, given the current share supply
         share_type max_force_settlement_volume(share_type current_supply)const;

         /** return true if there has been a black swan, false otherwise */
         bool has_settlement()const { return !settlement_price.is_null(); }

         /**
          *  In the event of a black swan, the swan price is saved in the settlement price, and all margin positions
          *  are settled at the same price with the siezed collateral being moved into the settlement fund. From this
          *  point on no further updates to the asset are permitted (no feeds, etc) and forced settlement occurs
          *  immediately when requested, using the settlement price and fund.
          */
         ///@{
         /// Price at which force settlements of a black swanned asset will occur
         price settlement_price;
         /// Amount of collateral which is available for force settlement
         share_type settlement_fund;
         ///@}

         time_point_sec feed_expiration_time()const
         { return current_feed_publication_time + options.feed_lifetime_sec; }
         bool feed_is_expired_before_hardfork_615(time_point_sec current_time)const
         { return feed_expiration_time() >= current_time; }
         bool feed_is_expired(time_point_sec current_time)const
         { return feed_expiration_time() <= current_time; }
         void update_median_feeds(time_point_sec current_time);
   };


   class guarantee_object :public abstract_object<guarantee_object>
   {
   public:
	   static const uint8_t space_id = implementation_ids;
	   static const uint8_t type_id = impl_guarantee_obj_type;

	   address owner_addr;
	   string  chain_type;
	   string time;
	   asset asset_orign;
	   asset asset_target;
	   asset asset_finished;
	   fc::flat_set<transaction_id_type> records;
	   bool finished;
   };
   struct by_feed_expiration;
   typedef multi_index_container<
      asset_bitasset_data_object,
      indexed_by<
         ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
         ordered_non_unique< tag<by_feed_expiration>,
            const_mem_fun< asset_bitasset_data_object, time_point_sec, &asset_bitasset_data_object::feed_expiration_time >
         >
      >
   > asset_bitasset_data_object_multi_index_type;
   //typedef flat_index<asset_bitasset_data_object> asset_bitasset_data_index;
   typedef generic_index<asset_bitasset_data_object, asset_bitasset_data_object_multi_index_type> asset_bitasset_data_index;

   struct by_symbol;
   struct by_type;
   struct by_issuer;
   typedef multi_index_container<
      asset_object,
      indexed_by<
         ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
         ordered_unique< tag<by_symbol>, member<asset_object, string, &asset_object::symbol> >,
         ordered_non_unique< tag<by_issuer>, member<asset_object, account_id_type, &asset_object::issuer > >,
         ordered_unique< tag<by_type>,
            composite_key< asset_object,
                const_mem_fun<asset_object, bool, &asset_object::is_market_issued>,
                member< object, object_id_type, &object::id >
            >
         >
      >
   > asset_object_multi_index_type;
   typedef generic_index<asset_object, asset_object_multi_index_type> asset_index;
   struct by_owner;
   struct by_symbol_owner;
   typedef multi_index_container<
	   guarantee_object,
	   indexed_by<
	   ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
	   ordered_non_unique< tag<by_symbol>, member<guarantee_object, string, &guarantee_object::chain_type> >,
	   ordered_non_unique< tag<by_owner>, member<guarantee_object, address, &guarantee_object::owner_addr> >,
	   ordered_non_unique< tag<by_symbol_owner>,
	                  composite_key< guarantee_object,
	                                 member<guarantee_object, address, &guarantee_object::owner_addr>,
	                                 member< guarantee_object, string, &guarantee_object::chain_type >
	                               >
	                     >
                >
   > guarantee_object_multi_index_type;
   typedef generic_index<guarantee_object, guarantee_object_multi_index_type> guarantee_index;


} } // graphene::chain
FC_REFLECT_ENUM(graphene::chain::asset_object::public_asset_type, (normal_type)(crosschain_type))
FC_REFLECT_DERIVED( graphene::chain::asset_dynamic_data_object, (graphene::db::object),
                    (current_supply)(confidential_supply)(accumulated_fees)(fee_pool)(withdraw_limition)(gas_price))

FC_REFLECT_DERIVED( graphene::chain::asset_bitasset_data_object, (graphene::db::object),
                    (feeds)
                    (current_feed)
                    (current_feed_publication_time)
                    (options)
                    (force_settled_volume)
                    (is_prediction_market)
                    (settlement_price)
                    (settlement_fund)
                  )

FC_REFLECT_DERIVED( graphene::chain::asset_object, (graphene::db::object),
                    (symbol)
                    (precision)
                    (issuer)
                    (options)
                    (dynamic_asset_data_id)
					(feeds)
	                (publishers)
					(current_feed)
					(current_feed_publication_time)
	                (allow_withdraw_deposit)
                    (bitasset_data_id)
					(asset_type)
                    (buyback_account)
                  )

	FC_REFLECT_DERIVED(graphene::chain::guarantee_object, (graphene::db::object),
	    (owner_addr)
		(chain_type)
		(time)
		(asset_orign)
		(asset_target)
		(asset_finished)
		(records)
		(finished)
	)
	
