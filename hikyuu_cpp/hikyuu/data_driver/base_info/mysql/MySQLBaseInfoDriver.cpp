/*
 * MySQLBaseInfoDriver.cpp
 *
 *  Created on: 2014年8月27日
 *      Author: fasiondog
 */

#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include "MySQLBaseInfoDriver.h"

#include "../../../StockManager.h"
#include "../../../Log.h"
#include "../table/MarketInfoTable.h"
#include "../table/StockTypeInfoTable.h"
#include "../table/StockWeightTable.h"
#include "../table/StockTable.h"
#include "../table/HolidayTable.h"

namespace hku {

MySQLBaseInfoDriver::MySQLBaseInfoDriver() : BaseInfoDriver("mysql"), m_pool(nullptr) {}

MySQLBaseInfoDriver::~MySQLBaseInfoDriver() {
    if (m_pool) {
        delete m_pool;
    }
}

bool MySQLBaseInfoDriver::_init() {
    HKU_ASSERT_M(m_pool == nullptr, "Maybe repeat initialization!");
    Parameter connect_param;
    connect_param.set<string>("host", getParamFromOther<string>(m_params, "host", "127.0.0.1"));
    connect_param.set<string>("usr", getParamFromOther<string>(m_params, "usr", "root"));
    connect_param.set<string>("pwd", getParamFromOther<string>(m_params, "pwd", ""));
    connect_param.set<string>("db", getParamFromOther<string>(m_params, "db", "hku_base"));
    string port_str = getParamFromOther<string>(m_params, "port", "3306");
    unsigned int port = boost::lexical_cast<unsigned int>(port_str);
    connect_param.set<int>("port", port);
    m_pool = new ConnectPool<MySQLConnect>(connect_param);
    HKU_CHECK(m_pool, "Failed malloc ConnectPool!");
    return true;
}

vector<MarketInfo> MySQLBaseInfoDriver::getAllMarketInfo() {
    vector<MarketInfo> result;
    HKU_ERROR_IF_RETURN(!m_pool, result, "Connect pool ptr is null!");

    try {
        auto con = m_pool->getConnect();
        vector<MarketInfoTable> infoTables;
        con->batchLoad(infoTables);
        for (auto &info : infoTables) {
            try {
                result.emplace_back(info.market(), info.name(), info.description(), info.code(),
                                    info.lastDate(), info.openTime1(), info.closeTime1(),
                                    info.openTime2(), info.closeTime2());
            } catch (std::exception &e) {
                HKU_ERROR("Failed load market, {}", e.what());
            } catch (...) {
                HKU_ERROR_UNKNOWN;
            }
        }

    } catch (std::exception &e) {
        HKU_FATAL("load Market table failed! {}", e.what());
    } catch (...) {
        HKU_FATAL("load Market table failed!");
    }

    return result;
}

vector<StockTypeInfo> MySQLBaseInfoDriver::getAllStockTypeInfo() {
    vector<StockTypeInfo> result;
    HKU_ERROR_IF_RETURN(!m_pool, result, "Connect pool ptr is null!");

    try {
        auto con = m_pool->getConnect();
        vector<StockTypeInfoTable> infoTables;
        con->batchLoad(infoTables);
        for (auto &info : infoTables) {
            result.emplace_back(info.type(), info.description(), info.tick(), info.tickValue(),
                                info.precision(), info.minTradeNumber(), info.maxTradeNumber());
        }

    } catch (std::exception &e) {
        HKU_FATAL("load StockTypeInfo table failed! {}", e.what());
    } catch (...) {
        HKU_FATAL("load StockTypeInfo table failed!");
    }

    return result;
}

StockWeightList MySQLBaseInfoDriver::getStockWeightList(const string &market, const string &code,
                                                        Datetime start, Datetime end) {
    StockWeightList result;
    HKU_ASSERT(m_pool);

    try {
        auto con = m_pool->getConnect();
        HKU_CHECK(con, "Failed fetch connect!");

        vector<StockWeightTable> table;
        Datetime new_end = end.isNull() ? Datetime::max() : end;
        con->batchLoad(
          table,
          format(
            "stockid=(select stockid from stock where marketid=(select marketid from "
            "market where market='{}') and code='{}') and date>={} and date<{} order by date asc",
            market, code, start.year() * 10000 + start.month() * 100 + start.day(),
            new_end.year() * 10000 + new_end.month() * 100 + new_end.day()));

        for (auto &w : table) {
            try {
                result.push_back(StockWeight(Datetime(w.date * 10000), w.countAsGift * 0.0001,
                                             w.countForSell * 0.0001, w.priceForSell * 0.001,
                                             w.bonus * 0.001, w.countOfIncreasement * 0.0001,
                                             w.totalCount, w.freeCount));
            } catch (std::out_of_range &e) {
                HKU_WARN("Date of id({}) is invalid! {}", w.id(), e.what());
            } catch (std::exception &e) {
                HKU_WARN("Error StockWeight Record id({}) {}", w.id(), e.what());
            } catch (...) {
                HKU_WARN("Error StockWeight Record id({})! Unknown reason!", w.id());
            }
        }

    } catch (std::exception &e) {
        HKU_FATAL("load StockWeight table failed! {}", e.what());
        return result;
    } catch (...) {
        HKU_FATAL("load StockWeight table failed!");
        return result;
    }

    return result;
}

vector<StockInfo> MySQLBaseInfoDriver::getAllStockInfo() {
    vector<StockInfo> result;
    HKU_ERROR_IF_RETURN(!m_pool, result, "Connect pool ptr is null!");
    try {
        auto con = m_pool->getConnect();
        con->batchLoad(result);
    } catch (...) {
    }
    return result;
}

StockInfo MySQLBaseInfoDriver::getStockInfo(string market, const string &code) {
    HKU_ASSERT(m_pool);
    StockInfo result;
    try {
        to_upper(market);
        auto con = m_pool->getConnect();
        string sql =
          format("{} and a.code='{}' and c.market='{}'", StockInfo::getSelectSQL(), code, market);
        SQLStatementPtr st = con->getStatement(sql);
        st->exec();
        if (st->moveNext()) {
            result.load(st);
        }
    } catch (...) {
    }
    return result;
}

MarketInfo MySQLBaseInfoDriver::getMarketInfo(const string &market) {
    MarketInfo result;
    HKU_ERROR_IF_RETURN(!m_pool, result, "Connect pool ptr is null!");
    auto con = m_pool->getConnect();
    try {
        MarketInfoTable info;
        string new_market(market);
        to_upper(new_market);
        con->load(info, format("market=\"{}\"", new_market));
        if (!info.market().empty()) {
            result = MarketInfo(info.market(), info.name(), info.description(), info.code(),
                                info.lastDate(), info.openTime1(), info.closeTime1(),
                                info.openTime2(), info.closeTime2());
        }
    } catch (...) {
    }
    return result;
}

StockTypeInfo MySQLBaseInfoDriver::getStockTypeInfo(uint32_t type) {
    StockTypeInfo result;
    HKU_ERROR_IF_RETURN(!m_pool, result, "Connect pool ptr is null!");
    auto con = m_pool->getConnect();
    try {
        StockTypeInfoTable info;
        con->load(info, format("type={}", type));
        if (info.type() != Null<uint32_t>()) {
            result = StockTypeInfo(info.type(), info.description(), info.tick(), info.tickValue(),
                                   info.precision(), info.minTradeNumber(), info.maxTradeNumber());
        }
    } catch (...) {
    }
    return result;
}

std::unordered_set<Datetime> MySQLBaseInfoDriver::getAllHolidays() {
    HKU_ASSERT(m_pool);
    std::unordered_set<Datetime> result;
    try {
        auto con = m_pool->getConnect();
        std::vector<HolidayTable> holidays;
        con->batchLoad(holidays);
        for (auto &holiday : holidays) {
            try {
                result.insert(holiday.datetime());
            } catch (std::exception &e) {
                HKU_WARN(e.what());
            } catch (...) {
                HKU_ERROR_UNKNOWN;
            }
        }
    } catch (...) {
    }
    return result;
}

Parameter MySQLBaseInfoDriver::getFinanceInfo(const string &market, const string &code) {
    Parameter result;
    HKU_IF_RETURN(!m_pool, result);

    std::stringstream buf;
    buf << "select f.updated_date, f.ipo_date, f.province,"
        << "f.industry, f.zongguben, f.liutongguben, f.guojiagu, f.faqirenfarengu,"
        << "f.farengu, f.bgu, f.hgu, f.zhigonggu, f.zongzichan, f.liudongzichan,"
        << "f.gudingzichan, f.wuxingzichan, f.gudongrenshu, f.liudongfuzhai,"
        << "f.changqifuzhai, f.zibengongjijin, f.jingzichan, f.zhuyingshouru,"
        << "f.zhuyinglirun, f.yingshouzhangkuan, f.yingyelirun, f.touzishouyu,"
        << "f.jingyingxianjinliu, f.zongxianjinliu, f.cunhuo, f.lirunzonghe,"
        << "f.shuihoulirun, f.jinglirun, f.weifenpeilirun, f.meigujingzichan,"
        << "f.baoliu2 from stkfinance f, stock s, market m "
        << "where m.market='" << market << "'"
        << " and s.code = '" << code << "'"
        << " and s.marketid = m.marketid"
        << " and f.stockid = s.stockid"
        << " order by updated_date DESC limit 1";

    auto con = m_pool->getConnect();

    auto st = con->getStatement(buf.str());
    st->exec();
    if (!st->moveNext()) {
        return result;
    }

    int updated_date(0), ipo_date(0);
    price_t province(0), industry(0), zongguben(0), liutongguben(0), guojiagu(0), faqirenfarengu(0);
    price_t farengu(0), bgu(0), hgu(0), zhigonggu(0), zongzichan(0), liudongzichan(0),
      gudingzichan(0);
    price_t wuxingzichan(0), gudongrenshu(0), liudongfuzhai(0), changqifuzhai(0), zibengongjijin(0);
    price_t jingzichan(0), zhuyingshouru(0), zhuyinglirun(0), yingshouzhangkuan(0), yingyelirun(0);
    price_t touzishouyi(0), jingyingxianjinliu(0), zongxianjinliu(0), cunhuo(0), lirunzonghe(0);
    price_t shuihoulirun(0), jinglirun(0), weifenpeilirun(0), meigujingzichan(0), baoliu2(0);

    st->getColumn(0, updated_date, ipo_date, province, industry, zongguben, liutongguben, guojiagu,
                  faqirenfarengu, farengu, bgu, hgu, zhigonggu, zongzichan, liudongzichan,
                  gudingzichan, wuxingzichan, gudongrenshu, liudongfuzhai, changqifuzhai,
                  zibengongjijin, jingzichan, zhuyingshouru, zhuyinglirun, yingshouzhangkuan,
                  yingyelirun, touzishouyi, jingyingxianjinliu, zongxianjinliu, cunhuo, lirunzonghe,
                  shuihoulirun, jinglirun, weifenpeilirun, meigujingzichan, baoliu2);

    result.set<string>("market", market);
    result.set<string>("code", code);
    result.set<int>("updated_date", updated_date);
    result.set<int>("ipo_date", ipo_date);
    result.set<price_t>("province", province);
    result.set<price_t>("industry", industry);
    result.set<price_t>("zongguben", zongguben);
    result.set<price_t>("liutongguben", liutongguben);
    result.set<price_t>("guojiagu", guojiagu);
    result.set<price_t>("faqirenfarengu", faqirenfarengu);
    result.set<price_t>("farengu", farengu);
    result.set<price_t>("bgu", bgu);
    result.set<price_t>("hgu", hgu);
    result.set<price_t>("zhigonggu", zhigonggu);
    result.set<price_t>("zongzichan", zongzichan);
    result.set<price_t>("liudongzichan", liudongzichan);
    result.set<price_t>("gudingzichan", gudingzichan);
    result.set<price_t>("wuxingzichan", wuxingzichan);
    result.set<price_t>("gudongrenshu", gudongrenshu);
    result.set<price_t>("liudongfuzhai", liudongfuzhai);
    result.set<price_t>("changqifuzhai", changqifuzhai);
    result.set<price_t>("zibengongjijin", zibengongjijin);
    result.set<price_t>("jingzichan", jingzichan);
    result.set<price_t>("zhuyingshouru", zhuyingshouru);
    result.set<price_t>("zhuyinglirun", zhuyinglirun);
    result.set<price_t>("yingshouzhangkuan", yingshouzhangkuan);
    result.set<price_t>("yingyelirun", yingyelirun);
    result.set<price_t>("touzishouyi", touzishouyi);
    result.set<price_t>("jingyingxianjinliu", jingyingxianjinliu);
    result.set<price_t>("zongxianjinliu", zongxianjinliu);
    result.set<price_t>("cunhuo", cunhuo);
    result.set<price_t>("lirunzonghe", lirunzonghe);
    result.set<price_t>("shuihoulirun", shuihoulirun);
    result.set<price_t>("jinglirun", jinglirun);
    result.set<price_t>("weifenpeilirun", weifenpeilirun);
    result.set<price_t>("meigujingzichan", meigujingzichan);
    result.set<price_t>("baoliu2", baoliu2);
    return result;
}

} /* namespace hku */
