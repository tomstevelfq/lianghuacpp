#include "tdef.h"
#include "GTPTradeApi.h"
#include "GTPTradeApiConst.h"
#include "GTPTradeApiStruct.h"

#include "GTPMarketDataApi.h"
#include "sipv2_i.h"
#include "tszmarket.h"
#include "tshmarket.h"

#include "inireader.h"


#ifdef DEBUG_PRINT
#include "DebugPrint.hpp"
#endif

#include <iostream>
#include <string>
#include <regex>
#include <chrono>
#include <thread>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include<cstring>
#include<string>
#include<thread>
#include <chrono>
#include<Windows.h>
#include <mutex>
#include <thread>
#include <future>
using namespace std;

GTPTradeApi * g_TradeApi;
GTPMarketDataApi *g_marketDataAPI;
std::string g_customCode;
std::string g_passwd;

// 普通户的股东账号
std::string g_acct;
std::string g_shTrdAcct;
std::string g_szTrdAcct;

// 信用户即两融的股东账号
std::string g_acctCredit;
std::string g_shTrdAcctCredit;
std::string g_szTrdAcctCredit;

//股东账号，可以调用OnReqTradeAccount查询出来，它是固定的，可以在配置文件里面写死
//用户可以使用demo.exe 在命令行对话框中 敲 QUERY ACCOUNT 自己查询

static int g_Connected = 0;
static int g_LogOn = 0;

class StkSpi :public CGTPTradeSpi
{
public:
    virtual void OnRspLogon(int nRequest, const CRspErrorField *pError, bool bLast, const COXRspLogonField *pField) override
    {
#ifdef DEBUG_PRINT
        PrintCOXRspLogonField(pField);
#endif

        printf("func=[%s] called\n", __FUNCTION__);
        //所有的OnRsp回调函数，都要先判断 if (pError && pError->ErrorId) 为真，那么便意味着出错了，
        //此时，后面的pField 没有意义，不要去读它
        // ErrorInfo ,ExeInfo 这种提示信息字符串 是gb2312 编码，可能会乱码
        if (pError && pError->ErrorId != 0)
        {
            // 登录失败
            printf("errorid=[%d],errInfo=[%s]\n", pError->ErrorId, pError->ErrorInfo);
            return;
        }

        // 登录成功后，顺便查询一下该资金账号对应的 股东账号，股东账号是固定的，查出来以后，可以在配置文件里面写死
        {
            //用资金账号查股东账号(股票，两融，期权都支持)
            COXReqTradeAcctField req;
            memset(&req, 0, sizeof(req));
            req.AcctType = OX_ACCOUNT_STOCK;
            // 填资金账号
            snprintf(req.Account, sizeof(req.Account), pField->Account);
           
            // 注意看异步回调函数OnRspTradeAccounts，我在里面取股东账号
            g_TradeApi->OnReqTradeAccounts(0, &req);
        }

        g_LogOn += 1;
        return;
    }

    virtual void OnRspTradeAccounts(int nRequest, const CRspErrorField *pError, bool bLast, const COXRspTradeAcctField *pField) override
    {
        printf("func=[%s] called\n", __FUNCTION__);
        if (pError && pError->ErrorId != 0)
        {
            printf("errorid=[%d],errInfo=[%s]\n", pError->ErrorId, pError->ErrorInfo);
            return;
        }
        //pField->BoardId=="00" 表示 深圳 A股
        //pField->BoardId=="10" 表示 上海 A股
        // 其他 例如 "03","13"表示深港通，沪港通，深B，沪B，很少用到，不做介绍
        printf("客户代码[%s],资金账号[%s],板块id[%s],股东账号[%s]\n", pField->CustCode, pField->Account, pField->BoardId, pField->TrdAcct);

        // 我这里是因为 同时登录了 2个资金账号，普通和信用，所以 要分别判断取股东账号
     
        if (g_acct == pField->Account )
        {
            if (strncmp(pField->BoardId, "00", 2) == 0)
            {
                g_szTrdAcct = pField->TrdAcct;
            }

            if (strncmp(pField->BoardId, "10", 2) == 0)
            {
                g_shTrdAcct = pField->TrdAcct;
            }
        }
        
        if (g_acctCredit == pField->Account)
        {
            if (strncmp(pField->BoardId, "00", 2) == 0)
            {
                g_szTrdAcctCredit = pField->TrdAcct;
            }
            
            if (strncmp(pField->BoardId, "10", 2) == 0)
            {
                g_shTrdAcctCredit = pField->TrdAcct;
            }
        }
#ifdef DEBUG_PRINT
        PrintCOXRspTradeAcctField(pField);
#endif
    }

    //委托信息推送
    virtual void OnRtnOrder(const COXOrderTicket *pRtnOrderTicket) override
    {
        std::cout << __FUNCTION__ << " Symbol : " << pRtnOrderTicket->Symbol << " orderPrice : "
            << pRtnOrderTicket->OrderPrice << " orderQty: " << pRtnOrderTicket->OrderQty
            << " FilledQty : " << pRtnOrderTicket->FilledQty << " canceledQty : " << pRtnOrderTicket->CanceledQty
            << " orderState : " << pRtnOrderTicket->OrderState << " OrderID : " << pRtnOrderTicket->OrderId 
            << " AcctSN:" << pRtnOrderTicket->AcctSN << " OrderBSN:" << pRtnOrderTicket->OrderBsn << std::endl;
#ifdef DEBUG_PRINT
        PrintCOXOrderTicket(pRtnOrderTicket);
#endif
    }
    //成交信息推送
    virtual void OnRtnOrderFilled(const COXOrderFilledField *pFilledInfo) override
    {
        std::cout << __FUNCTION__ << " symbol: " << pFilledInfo->Symbol << " FilledPrice : " << pFilledInfo->FilledPrice
            << " FilledQty : " << pFilledInfo->FilledQty << " orderID : " << pFilledInfo->OrderId 
            << " AcctSN:" << pFilledInfo->AcctSN << " OrderBSN:" << pFilledInfo->OrderBsn << std::endl;

#ifdef DEBUG_PRINT
        PrintCOXOrderFilledField(pFilledInfo);
#endif
    }
    // ...

    virtual void OnRspCancelTicket(int nRequest, const CRspErrorField *pError, const COXRspCancelTicketField * pField) override
    {
        printf("func=[%s] called\n", __FUNCTION__);
        if (pError && pError->ErrorId != 0)
        {
            printf("errorid=[%d],errInfo=[%s]\n", pError->ErrorId, pError->ErrorInfo);
            return;
        }

        if (pField)
        {
            std::cout << " symbol : " << pField->Symbol << " exeInfo : " << pField->ExeInfo
                << " orderID: " << pField->OrderId << "OrderQty ： " << pField->OrderQty << std::endl;
        }
#ifdef DEBUG_PRINT
        PrintCOXRspCancelTicketField(pField);
#endif
    }

    virtual void OnRspBatchCancelTicket(int nRequest, const CRspErrorField* pError, const COXRspBatchCancelTicketField * pField) override
    {
        printf("func=[%s] called\n", __FUNCTION__);
        if (pError && pError->ErrorId != 0)
        {
            printf("errorid=[%d],errInfo=[%s]\n", pError->ErrorId, pError->ErrorInfo);
            return;
        }

        if (pField)
            printf("ret=[%d],bsn=[%d],orderId=[%s],retInfo=[%s]\n", pField->CancelRet, pField->OrderBsn, pField->OrderId, pField->RetInfo);
#ifdef DEBUG_PRINT
        PrintCOXRspBatchCancelTicketField(pField);
#endif
    }

    virtual void OnRspQueryOrders(int nRequest, const CRspErrorField *pError, bool bLast, const COXRspOrderField *pField) override
    {
        printf("func=[%s] called\n", __FUNCTION__);
        if (pError && pError->ErrorId != 0 && pError->ErrorId != 200)
        {
            printf("errorid=[%d],errInfo=[%s]\n", pError->ErrorId, pError->ErrorInfo);
            return;
        }

        if (pField)
        {
            std::cout << " orderID: " << pField->OrderId << " symbol " << pField->Symbol << " price: " << pField->OrderPrice
                << " filledQty: " << pField->FilledQty << " canceledQty: " << pField->CanceledQty << std::endl;
        }
#ifdef DEBUG_PRINT
        PrintCOXRspOrderField(pField);
#endif
    }

    virtual void OnRspQueryBalance(int nRequest, const CRspErrorField *pError, bool bLast, const COXRspBalanceField *pField) override
    {
        printf("func=[%s] called\n", __FUNCTION__);
        if (pError && pError->ErrorId != 0)
        {
            printf("errorid=[%d],errInfo=[%s]\n", pError->ErrorId, pError->ErrorInfo);
            return;
        }

        if (pField)
        {
            std::cout << " AcctWorth: " << pField->AccountWorth << " FundValue " << pField->FundValue << " MarketValue: " << pField->MarketValue
                << "Available : " << pField->Available << std::endl;
        }
#ifdef DEBUG_PRINT
        PrintCOXRspBalanceField(pField);
#endif
    }

    virtual void OnRspQueryPositions(int nRequest, const CRspErrorField *pError, bool bLast, const COXRspPositionField *pField) override
    {
        printf("func=[%s] called\n", __FUNCTION__);
        // 查询持仓，查询成交历史，成交明细的时候，如果账户没有持仓 或 没有 成交，那么 就返回ErrorId==200
        // 表示 “空”。“空”不算真正的错误，请自行辨别，后面有类似情况，不再赘叙
        if (pError && pError->ErrorId != 0 && pError->ErrorId != 200)
        {
            printf("errorid=[%d],errInfo=[%s]\n", pError->ErrorId, pError->ErrorInfo);
            return;
        }

        if (pField)
        {
            std::cout << " Symbol: " << pField->Symbol << " Avl: " << pField->StkAvl << " Freeze: " << pField->StkFrz << " TrdFreeze: " << pField->StkTrdFrz << std::endl;
        }
#ifdef DEBUG_PRINT
        PrintCOXRspPositionField(pField);
#endif
    }

    virtual void OnRspQueryFilledDetails(int nRequest, const CRspErrorField *pError, bool bLast, const COXRspFilledDetailField *pField) override
    {
        printf("func=[%s] called\n", __FUNCTION__);
        if (pError && pError->ErrorId != 0 && pError->ErrorId != 200)
        {
            printf("errorid=[%d],errInfo=[%s]\n", pError->ErrorId, pError->ErrorInfo);
            return;
        }

        if (pField)
        {
            std::cout << " Symbol: " << pField->Symbol << " FilledType: " << pField->FilledType << " qty: " << pField->FilledQty << " price: " << pField->FilledPrice << " orderID:" << pField->OrderId << std::endl;
        }
#ifdef DEBUG_PRINT
        PrintCOXRspFilledDetailField(pField);
#endif
    }

    // 融资融券 负债查询
    virtual void OnRspQueryCreditBalanceDebt(int nRequest, const CRspErrorField *pError, bool bLast, const COXRspCreditBalanceDebtField *pField) override
    {
        printf("func=[%s] called\n", __FUNCTION__);
        if (pError && pError->ErrorId != 0)
        {
            printf("errorid=[%d],errInfo=[%s]\n", pError->ErrorId, pError->ErrorInfo);
            return;
        }

        if (pField)
        {
            std::cout << "FiAmt:" << pField->FiAmt << "TotalFiFee:" << pField->TotalFiFee << "FICredit:" << pField->FICredit << std::endl;
        }
#ifdef DEBUG_PRINT
        PrintCOXRspCreditBalanceDebtField(pField);
#endif
    }

    virtual void OnRspCreditRepay(int nRequest, const CRspErrorField *pError, const COXRspCreditRepay *pField) override
    {
        printf("func=[%s] called\n", __FUNCTION__);
        if (pError && pError->ErrorId != 0)
        {
            printf("errorid=[%d],errInfo=[%s]\n", pError->ErrorId, pError->ErrorInfo);
            return;
        }

        if (pField)
        {
            printf("cuacctCode=[%s],realRepayAmt=[%s],currency=[%c],repayContractAmt=[%s]\n",
                pField->CuacctCode, pField->RealRepayAmt, pField->Currency, pField->RepayContractAmt);
        }
#ifdef DEBUG_PRINT
        PrintCOXRspCreditRepay(pField);
#endif
    }

    virtual void OnRspQueryCreditContracts(int nRequest, const CRspErrorField *pError, bool bLast, const COXRspCreditContractField *pField) override
    {
        printf("func=[%s] called\n", __FUNCTION__);
        if (pError && pError->ErrorId != 0 && pError->ErrorId != 200)
        {
            printf("errorid=[%d],errInfo=[%s]\n", pError->ErrorId, pError->ErrorInfo);
            return;
        }

        if (pField)
        {
            printf("bLast=[%d],trdacct=[%s] stk_code=[%s],orderId=[%s],contract_status=[%c]\n",
                bLast, pField->Trdacct, pField->Symbol, pField->OrderId, pField->ContractStatus);
        }
#ifdef DEBUG_PRINT
        PrintCOXRspCreditContractField(pField);
#endif
    }

    virtual void OnRspCompositeOrder(int nRequest, const CRspErrorField *pError, const COXRspCompositeOrderField * pField)
    {
        printf("func=[%s] called\n", __FUNCTION__);
        if (pError && pError->ErrorId != 0)
        {
            printf("errorid=[%d],errInfo=[%s]\n", pError->ErrorId, pError->ErrorInfo);
            return;
        }

        for (unsigned i = 0; i < pField->TotalCount; ++i)
        {
            auto& singleOrder = pField->orderRetInfo[i];
            auto& ticket = singleOrder.orderTicket;

            printf("single_order_ret=[%d],single_order_exeInfo=[%s],single_order_bsn=[%d],single_order_symbol=[%s]\n",
                singleOrder.OrderRet, ticket->ExeInfo, ticket->OrderBsn, ticket->Symbol);
        }

    }

    virtual void OnRspETFBasketOrder(int nRequest, const CRspErrorField* pError, const COXRspCompositeOrderField* pField)
    {
        printf("func=[%s] called\n", __FUNCTION__);
        if (pError && pError->ErrorId != 0)
        {
            printf("errorid=[%d],errInfo=[%s]\n", pError->ErrorId, pError->ErrorInfo);
            return;
        }

        for (unsigned i = 0; i < pField->TotalCount; ++i)
        {
            auto& singleOrder = pField->orderRetInfo[i];
            auto& ticket = singleOrder.orderTicket;

            printf("single_order_ret=[%d],single_orderid=[%s],single_order_acctsn = [%d], single_order_bsn=[%d],single_order_symbol=[%s]\n",
                singleOrder.OrderRet, ticket->OrderId, ticket->AcctSN, ticket->OrderBsn, ticket->Symbol);
        }

    }

    virtual int OnConnected() override
    {
        // 交易连接上了
        g_Connected = 1;
        std::cout << __FUNCTION__ << " called" << std::endl;
        return 0;
    }

    virtual int OnDisconnected() override
    {
        // 交易连接断了
        // 我们已经处理了 断线自动重连，重新登录，因此 客户 无需在这里重新登录
        g_Connected = 0;
        std::cout << __FUNCTION__ << " called" << std::endl;
        return 0;
    }

    virtual void OnRspQueryETFInfo(int nRequest, const CRspErrorField * pError, bool bLast, const COXRspETFInfoField *pField) override
    {
        printf("func=[%s] called\n", __FUNCTION__);
        if (pError && pError->ErrorId != 0)
        {
            printf("errorid=[%d],errInfo=[%s]\n", pError->ErrorId, pError->ErrorInfo);
            return;
        }

        printf("func=[%s] called, etfCode=[%s], bLast=[%d]\n", __FUNCTION__, pField->ETFCode, bLast);
    }

    virtual void OnRspQueryETFBasket(int nRequest, const CRspErrorField * pError, bool bLast, const COXRspETFBasketInfoField *pField) override
    {
        printf("func=[%s] called\n", __FUNCTION__);
        if (pError && pError->ErrorId != 0)
        {
            printf("errorid=[%d],errInfo=[%s]\n", pError->ErrorId, pError->ErrorInfo);
            return;
        }

        printf("nRequest=[%d],pError=[%p],bLast=[%d],pField=[%p]\n", nRequest, pError, bLast, pField);
    }

    // 这个函数废弃了，新客户不会收到，忽略之
    virtual void OnOrderTicketError(int nRequest, const CRspErrorField *pError)
    {
        printf("func=[%s] called\n", __FUNCTION__);
        if (pError && pError->ErrorId != 0)
        {
            printf("errorid=[%d],errInfo=[%s]\n", pError->ErrorId, pError->ErrorInfo);
            return;
        }

        printf("nRequest=[%d],pError.id=[%d],pError.info=[%s]\n", nRequest, pError->ErrorId, pError->ErrorInfo);
    }

};

bool isEmpty(const char* path) {
	FILE *file = fopen(path, "r");  // 尝试以只读模式打开文件
	if (file) {
		fclose(file);  // 文件打开成功后记得关闭
		return false;
	}
	else {
		return true;
	}
}

bool isFileSizeZero(std::fstream& file) {
	// 首先检查文件是否打开
	if (!file.is_open()) {
		std::cerr << "File is not open." << std::endl;
		return false;
	}
	std::streampos fileSize = file.tellg();
	return fileSize == (std::streampos)0;
}

// 写入数据到文件
void WriteMarketDataToFile(const T_SZ_StockMarketDataL1& data, const std::string& filename) {
	std::fstream file(filename, std::ios::app);  // 打开文件，以追加模式写入
	if (!file.is_open()) {
		std::cerr << "Failed to open file: " << filename << std::endl;
		return;
	}

	// 将数据格式化为逗号分隔的格式并写入文件
	file << data.nActionDay << ","
		<< data.nTime << ","
		<< data.nStatus << ","
		<< data.uPreClose << ","
		<< data.uOpen << ","
		<< data.uHigh << ","
		<< data.uLow << ","
		<< data.uMatch << ",";

	// 申卖价和申卖量
	for (int i = 0; i < 5; ++i) {
		file << data.uAskPrice[i] << " ";
	}
	file << ",";
	for (int i = 0; i < 5; ++i) {
		file << data.uAskVol[i] << " ";
	}
	file << ",";

	// 申买价和申买量
	for (int i = 0; i < 5; ++i) {
		file << data.uBidPrice[i] << " ";
	}
	file << ",";
	for (int i = 0; i < 5; ++i) {
		file << data.uBidVol[i] << " ";
	}
	file << ",";

	// 其他字段
	file << data.uNumTrades << ","
		<< data.iVolume << ","
		<< data.iTurnover << ","
		<< data.uHighLimited << ","
		<< data.uLowLimited << ","
		<< data.sTradingPhraseCode << ","
		<< data.nPreIOPV << ","
		<< data.nIOPV << "\n";

	file.close();
}


// 写入数据到文件
void WriteMarketDataL2ToFile(const T_SH_StockMarketDataL2& data, const std::string& filename) {
	std::ofstream file(filename, std::ios::app);  // 打开文件，以追加模式写入
	if (!file.is_open()) {
		std::cerr << "Failed to open file: " << filename << std::endl;
		return;
	}

	// 写入数据
	file << data.nActionDay << ","
		<< data.nTime << ","
		<< data.nStatus << ","
		<< data.uPreClose << ","
		<< data.uOpen << ","
		<< data.uHigh << ","
		<< data.uLow << ","
		<< data.uMatch << ",";

	// 申卖价和申卖量
	for (int i = 0; i < 10; ++i) {
		file << data.uAskPrice[i] << " ";
	}
	file << ",";
	for (int i = 0; i < 10; ++i) {
		file << data.uAskVol[i] << " ";
	}
	file << ",";

	// 申买价和申买量
	for (int i = 0; i < 10; ++i) {
		file << data.uBidPrice[i] << " ";
	}
	file << ",";
	for (int i = 0; i < 10; ++i) {
		file << data.uBidVol[i] << " ";
	}
	file << ",";

	// 其他字段
	file << data.uNumTrades << ","
		<< data.iVolume << ","
		<< data.iTurnover << ","
		<< data.iTotalBidVol << ","
		<< data.iTotalAskVol << ","
		<< data.uWeightedAvgBidPrice << ","
		<< data.uWeightedAvgAskPrice << ","
		<< data.nIOPV << ","
		<< data.nYieldToMaturity << ","
		<< data.uHighLimited << ","
		<< data.uLowLimited << ","
		<< data.sPrefix << ","
		<< data.nSyl1 << ","
		<< data.nSyl2 << ","
		<< data.nSD2 << ","
		<< data.sTradingPhraseCode << ","
		<< data.nPreIOPV << "\n";

	file.close();
}

// 写入数据到文件
void WriteMarketDataL1ToFile(const T_SH_StockMarketDataL1& data, const std::string& filename) {
	// 如果文件为空，写入表头
	if (isEmpty(filename.c_str())) {
		std::ofstream file(filename, std::ios::app);  // 打开文件，以追加模式写入
		if (!file.is_open()) {
			std::cerr << "Failed to open file: " << filename << std::endl;
			return;
		}
		file << "Date,Time,Status,PreClose,Open,High,Low,Match,AskPrices,AskVolumes,BidPrices,BidVolumes,"
			"NumTrades,Volume,Turnover,HighLimited,LowLimited,TradingPhaseCode,PreIOPV,IOPV\n";
		file.close();
	}

	std::ofstream file(filename, std::ios::app);  // 打开文件，以追加模式写入
	if (!file.is_open()) {
		std::cerr << "Failed to open file: " << filename << std::endl;
		return;
	}

	// 写入数据
	file << data.nActionDay << ","
		<< data.nTime << ","
		<< data.nStatus << ","
		<< data.uPreClose << ","
		<< data.uOpen << ","
		<< data.uHigh << ","
		<< data.uLow << ","
		<< data.uMatch << ",";

	// 申卖价和申卖量
	for (int i = 0; i < 5; ++i) {
		file << data.uAskPrice[i] << " ";
	}
	file << ",";
	for (int i = 0; i < 5; ++i) {
		file << data.uAskVol[i] << " ";
	}
	file << ",";

	// 申买价和申买量
	for (int i = 0; i < 5; ++i) {
		file << data.uBidPrice[i] << " ";
	}
	file << ",";
	for (int i = 0; i < 5; ++i) {
		file << data.uBidVol[i] << " ";
	}
	file << ",";

	// 其他字段
	file << data.uNumTrades << ","
		<< data.iVolume << ","
		<< data.iTurnover << ","
		<< data.uHighLimited << ","
		<< data.uLowLimited << ","
		<< data.sTradingPhaseCode << ","
		<< data.nPreIOPV << ","
		<< data.nIOPV << "\n";

	file.close();
}

// 写入数据到文件
void WriteSZMarketDataL2ToFile(const T_SZ_StockMarketDataL2& data, const std::string& filename) {

	// 如果文件为空，写入表头
	if (isEmpty(filename.c_str())) {
		std::ofstream file(filename, std::ios::app);  // 打开文件，以追加模式写入
		if (!file.is_open()) {
			std::cerr << "Failed to open file: " << filename << std::endl;
			return;
		}
		file << "Date,Time,Status,PreClose,Open,High,Low,Match,AskPrices,AskVolumes,BidPrices,BidVolumes,"
			"NumTrades,Volume,Turnover,TotalBidVol,TotalAskVol,WeightedAvgBidPrice,WeightedAvgAskPrice,"
			"IOPV,YieldToMaturity,HighLimited,LowLimited,Prefix,Syl1,Syl2,SD2,TradingPhraseCode,PreIOPV\n";
		file.close();
	}

	std::ofstream file(filename, std::ios::app);  // 打开文件，以追加模式写入
	if (!file.is_open()) {
		std::cerr << "Failed to open file: " << filename << std::endl;
		return;
	}

	// 写入数据
	file << data.nActionDay << ","
		<< data.nTime << ","
		<< data.nStatus << ","
		<< data.uPreClose << ","
		<< data.uOpen << ","
		<< data.uHigh << ","
		<< data.uLow << ","
		<< data.uMatch << ",";

	// 申卖价和申卖量
	for (int i = 0; i < 10; ++i) {
		file << data.uAskPrice[i] << " ";
	}
	file << ",";
	for (int i = 0; i < 10; ++i) {
		file << data.uAskVol[i] << " ";
	}
	file << ",";

	// 申买价和申买量
	for (int i = 0; i < 10; ++i) {
		file << data.uBidPrice[i] << " ";
	}
	file << ",";
	for (int i = 0; i < 10; ++i) {
		file << data.uBidVol[i] << " ";
	}
	file << ",";

	// 其他字段
	file << data.uNumTrades << ","
		<< data.iVolume << ","
		<< data.iTurnover << ","
		<< data.iTotalBidVol << ","
		<< data.iTotalAskVol << ","
		<< data.uWeightedAvgBidPrice << ","
		<< data.uWeightedAvgAskPrice << ","
		<< data.nIOPV << ","
		<< data.nYieldToMaturity << ","
		<< data.uHighLimited << ","
		<< data.uLowLimited << ","
		<< data.sPrefix << ","
		<< data.nSyl1 << ","
		<< data.nSyl2 << ","
		<< data.nSD2 << ","
		<< data.sTradingPhraseCode << ","
		<< data.nPreIOPV << "\n";

	file.close();
}

// 写入数据到文件
void WriteSZStepTradeToFile(const T_SZ_StockStepTrade& data, const std::string& filename) {
	// 如果文件为空，写入表头
	if (isEmpty(filename.c_str())) {
		std::ofstream file(filename, std::ios::app);  // 打开文件，以追加模式写入
		if (!file.is_open()) {
			std::cerr << "Failed to open file: " << filename << std::endl;
			return;
		}
		file << "ActionDay,ChannelNo,ApplSeqNum,MDStreamID,BidApplSeqNum,OfferApplSeqNum,"
			<< "SecurityID,SecurityIDSource,LastPx,LastQty,ExecType,TransactTime,ExtendFields\n";
		file.close();
	}

	std::ofstream file(filename, std::ios::app);  // 打开文件，以追加模式写入
	if (!file.is_open()) {
		std::cerr << "Failed to open file: " << filename << std::endl;
		return;
	}
	// 写入数据
	// 将 sMDStreamID 转换为 std::string 并确保以 '\0' 结尾
	std::string mdStreamID(data.sMDStreamID, 3);
	file << data.nActionDay << ","
		<< data.usChannelNo << ","
		<< data.i64ApplSeqNum << ","
		<< mdStreamID << ","
		<< data.i64BidApplSeqNum << ","
		<< data.i64OfferApplSeqNum << ","
		<< data.sSecurityID << ","
		<< data.sSecurityIDSource << ","
		<< data.i64LastPx << ","
		<< data.i64LastQty << ","
		<< data.cExecType << ","
		<< data.i64TransactTime;

	// 处理扩展字段（假设 sExtendFields 是一个以 '\0' 结尾的字符串）
	if (data.sExtendFields[0] != '\0') {
		file << "," << data.sExtendFields;
	}

	file << "\n";
	file.close();
}

// 写入数据到文件
void WriteSHStepTradeToFile(T_SH_StockStepTrade& data, const std::string& filename) {

	// 如果文件为空，写入表头
	if (isEmpty(filename.c_str())) {
		std::ofstream file(filename, std::ios::app);  // 打开文件，以追加模式写入
		if (!file.is_open()) {
			std::cerr << "Failed to open file: " << filename << std::endl;
			return;
		}
		file << "ActionDay,TradeIndex,TradeChannel,TradeTime,TradePrice,TradeQty,TradeMoney,"
			<< "TradeBuyNo,TradeSellNo,TradeBSflag\n";
		file.close();
	}

	std::ofstream file(filename, std::ios::app);  // 打开文件，以追加模式写入
	if (!file.is_open()) {
		std::cerr << "Failed to open file: " << filename << std::endl;
		return;
	}

	// 写入数据
	file << data.nActionDay << ","
		<< data.nTradeIndex << ","
		<< data.nTradeChannel << ","
		<< data.nTradeTime << ","
		<< data.nTradePrice << ","
		<< data.iTradeQty << ","
		<< data.iTradeMoney << ","
		<< data.iTradeBuyNo << ","
		<< data.iTradeSellNo << ","
		<< data.cTradeBSflag << "\n";

	file.close();
}

// 写入数据到文件
void WriteStockDataSHToFile(const StockDataSH& data, const std::string& filename,const char *code) {
	// 如果文件为空，写入表头
	if (isEmpty(filename.c_str())) {
		std::ofstream file(filename, std::ios::app);  // 打开文件，以追加模式写入
		if (!file.is_open()) {
			std::cerr << "Failed to open file: " << filename << std::endl;
			return;
		}
		file << "ActionDay,BizIndex,Channel,TickTime,Type,BuyOrderNo,SellOrderNo,Price,Qty,TradeMoney,TickBSflag,SecurityCode\n";
		file.close();
	}

	std::ofstream file(filename, std::ios::app);  // 打开文件，以追加模式写入
	if (!file.is_open()) {
		std::cerr << "Failed to open file: " << filename << std::endl;
		return;
	}

	// 写入数据
	file << data.nActionDay << ","
		<< data.iBizIndex << ","
		<< data.nChannel << ","
		<< data.nTickTime << ","
		<< data.cType << ","
		<< data.iBuyOrderNo << ","
		<< data.iSellOrderNo << ","
		<< data.nPrice << ","
		<< data.iQty << ","
		<< data.iTradeMoney << ","
		<< data.sTickBSflag << ","
		<<code<<"\n";

	file.close();
}

// 写入数据到文件
void WriteSZStepOrderToFile(const T_SZ_StockStepOrder& data, const std::string& filename) {
	// 如果文件为空，写入表头
	if (isEmpty(filename.c_str())) {
		std::ofstream file(filename, std::ios::app);  // 打开文件，以追加模式写入
		if (!file.is_open()) {
			std::cerr << "Failed to open file: " << filename << std::endl;
			return;
		}
		file << "ActionDay,ChannelNo,ApplSeqNum,MDStreamID,SecurityID,SecurityIDSource,Price,OrderQty,Side,TransactTime\n";
		file.close();
	}
	std::ofstream file(filename, std::ios::app);  // 打开文件，以追加模式写入
	if (!file.is_open()) {
		std::cerr << "Failed to open file: " << filename << std::endl;
		return;
	}

	// 将 sMDStreamID 转换为 std::string 并确保以 '\0' 结尾
	std::string mdStreamID(data.sMDStreamID, 3);
	// 写入数据
	file << data.nActionDay << ","
		<< static_cast<uint32_t>(data.usChannelNo) << ","  // 转换为 uint32_t 以避免符号扩展问题
		<< data.i64ApplSeqNum << ","
		<< mdStreamID << ","
		<< data.sSecurityID << ","
		<< data.sSecurityIDSource << ","
		<< data.i64Price << ","
		<< data.i64OrderQty << ","
		<< data.cSide << ","
		<< data.i64TransactTime << "\n";

	file.close();
}

// 全局或静态容器及互斥量，用于存储不同类型数据，按股票代码归类
static std::mutex mutexL1;
static std::map<std::string, std::vector<std::string>> stockDataMapL1; // 用于L1数据

static std::mutex mutexL2;
static std::map<std::string, std::vector<std::string>> stockDataMapL2; // 用于L2数据
static std::mutex mutexSH;
static std::mutex mutexSZZC;
static std::mutex mutexSZZW;
static std::map<std::string, vector<shared_ptr<StockDataSH>>> stockDataMapSH; // 用于SH数据

static std::map<std::string, vector<shared_ptr<Stock_Transaction_SZ>>> stockDataMapSZZC; // 用于SZZC

static std::map<std::string, vector<shared_ptr<Stock_StepOrder_SZ>>> stockDataMapSZZW; // 用于SZZW
const int WRITE_THRESHOLD = 100;

// 全局map，存储code对应的file对应的锁
std::map<std::string, std::mutex> mutex_map;

																	   // 写文件函数，写入完成后清空对应容器数据
void WriteToFileSH(const std::string &filepath, std::vector<shared_ptr<StockDataSH>> data,const string& code) {
	std::lock_guard<std::mutex> lock(mutex_map[code]);  // 使用lock_guard自动管理锁的生命周期
	std::ofstream ofs(filepath, std::ios::app);
	if (!ofs.is_open()) {
		std::cerr << "Failed to open file: " << filepath << std::endl;
		return;
	}
	for (auto it : data) {
		// 写入数据
		ofs << it->nActionDay << ","
			<< it->iBizIndex << ","
			<< it->nChannel << ","
			<< it->nTickTime << ","
			<< it->cType << ","
			<< it->iBuyOrderNo << ","
			<< it->iSellOrderNo << ","
			<< it->nPrice << ","
			<< it->iQty << ","
			<< it->iTradeMoney << ","
			<< it->sTickBSflag << ","
			<< code << "\n";
	}
	ofs.close();
}

// 写文件函数，写入完成后清空对应容器数据
void WriteToFileSZZC(const std::string &filepath, std::vector<shared_ptr<Stock_Transaction_SZ>> data, const string& code) {
	std::lock_guard<std::mutex> lock(mutex_map[code]);  // 使用lock_guard自动管理锁的生命周期
	std::ofstream ofs(filepath, std::ios::app);
	if (!ofs.is_open()) {
		std::cerr << "Failed to open file: " << filepath << std::endl;
		return;
	}
	for (auto it : data) {
		// 写入数据
		std::string mdStreamID(it->sMDStreamID, 3);
		ofs << it->nActionDay << ","
			<< it->usChannelNo << ","
			<< it->i64ApplSeqNum << ","
			<< mdStreamID << ","
			<< it->i64BidApplSeqNum << ","
			<< it->i64OfferApplSeqNum << ","
			<< it->sSecurityID << ","
			<< it->sSecurityIDSource << ","
			<< it->i64LastPx << ","
			<< it->i64LastQty << ","
			<< it->cExecType << ","
			<< it->i64TransactTime << ","
			<< it->sExtendFields << "\n";
	}
	ofs.close();
}

// 写文件函数，写入完成后清空对应容器数据
void WriteToFileSZZW(const std::string &filepath, std::vector<shared_ptr<Stock_StepOrder_SZ>> data, const string& code) {
	std::lock_guard<std::mutex> lock(mutex_map[code]);  // 使用lock_guard自动管理锁的生命周期
	std::ofstream ofs(filepath, std::ios::app);
	if (!ofs.is_open()) {
		std::cerr << "Failed to open file: " << filepath << std::endl;
		return;
	}
	for (auto it : data) {
		// 写入数据
		std::string mdStreamID(it->sMDStreamID, 3);
		ofs << it->nActionDay << ","
			<< it->usChannelNo << ","
			<< it->i64ApplSeqNum << ","
			<< mdStreamID << ","
			<< it->sSecurityID << ","
			<< it->sSecurityIDSource << ","
			<< it->sSecurityID << ","
			<< it->sSecurityIDSource << ","
			<< it->i64Price << ","
			<< it->i64OrderQty << ","
			<< it->cSide << ","
			<< it->i64TransactTime << ","
			<< it->sExtendFields << "\n";
	}
	ofs.close();
}

void StoreDataSH(const std::string &code, shared_ptr<StockDataSH> ptr)
{
	std::lock_guard<std::mutex> lock(mutexSH);
	stockDataMapSH[code].push_back(ptr);
	auto &vec = stockDataMapSH[code];
	vec.push_back(ptr);
	if (vec.size() >= WRITE_THRESHOLD) {
		// 构造文件路径：一个股票code对应一个文件
		std::string filepath = "./hangqing_data/" + code + ".csv";
		// 拷贝当前数据
		std::vector<shared_ptr<StockDataSH>> dataToWrite = vec;
		// 清空原数据
		vec.clear();
		// 写入文件
		WriteToFileSH(filepath, dataToWrite,code);
	}
}

void StoreDataSZZC(const std::string &code, shared_ptr<Stock_Transaction_SZ> ptr)
{
	bool needWrite = false;
	std::vector<std::shared_ptr<Stock_Transaction_SZ>> dataToWrite;
	std::string filepath;
	// 1) 缩小加锁的范围
	{
		std::lock_guard<std::mutex> lock(mutexSZZC);

		// 操作共享数据 stockDataMapSZZC
		stockDataMapSZZC[code].push_back(ptr);
		auto &vec = stockDataMapSZZC[code];
		vec.push_back(ptr);

		if (vec.size() >= WRITE_THRESHOLD) {
			// 需要写文件时，把要写的内容拷贝出来，并清空原始容器
			filepath = "./hangqing_data/" + code + "SZZC.csv";
			dataToWrite = vec;   // 拷贝要写入的对象
			vec.clear();

			// 标记一下，外部要执行写文件
			needWrite = true;
		}

	} // 2) 离开这个花括号，lock_guard 作用域结束，锁被自动解锁

	  // 3) 在锁外执行文件写入
	if (needWrite) {
		WriteToFileSZZC(filepath, dataToWrite, code);
	}
}

void StoreDataSZZW(const std::string &code, shared_ptr<Stock_StepOrder_SZ> ptr)
{
	std::lock_guard<std::mutex> lock(mutexSZZW);
	stockDataMapSZZW[code].push_back(ptr);
	auto &vec = stockDataMapSZZW[code];
	vec.push_back(ptr);
	if (vec.size() >= WRITE_THRESHOLD) {
		// 构造文件路径：一个股票code对应一个文件
		std::string filepath = "./hangqing_data/" + code + "SZZW.csv";
		// 拷贝当前数据
		std::vector<shared_ptr<Stock_StepOrder_SZ>> dataToWrite = vec;
		// 清空原数据
		vec.clear();
		// 写入文件
		WriteToFileSZZW(filepath, dataToWrite, code);
	}
}

//ptag是行情结构体，pParam为调用SetDoMsg时候传入的指针
void _cdecl MyDoMsg(T_SIPTAGMSG* ptag, void* pParam)
{
    // 行情相关的知识，请看 others目录下《中畅原生api客户接入文档.zip》
    // 里面有非常详细的解释
	string str = "./hangqing_data/";
    printf("func=[MyDoMsg],pParam=[%p]\n", pParam);
    char *pDataContent = (char*)ptag->MsgData;
    char* code = ptag->Code;
	cout << "code:" << code << endl;

    switch (ptag->MsgType)
    {
    case 1003: //上海逐笔委托，新加的，详细文档见gtp_release.zip压缩包，others目录<<中畅原生api文档>>
    {
        PSH_StockStepOrder data = reinterpret_cast<PSH_StockStepOrder>(pDataContent);
        printf("shanghai ZW,nOrderTime=[%d],nOrderPrice=[%d]\n", data->nOrderTime, data->nOrderPrice);
        break;
    }
    case 1005: // 上交所L1
    {
        PSH_StockMarketDataL1 data = reinterpret_cast<PSH_StockMarketDataL1>(pDataContent);
		str += ".csv";
		WriteMarketDataL1ToFile(*data, str.c_str());
        printf("shanghai L1, time=[%d],date=[%d]\n", data->nTime, data->nActionDay);
        break;
    }
    case 2005: //深交所L1
    {
        PSZ_StockMarketDataL1 data = reinterpret_cast<PSZ_StockMarketDataL1>(pDataContent);
		str += ".csv";
		WriteMarketDataToFile(*data, str.c_str());
        printf("shenzhen L1, time=[%d],date=[%d]\n", data->nTime, data->nActionDay);
        break;
    }
    case 1001: // 上交所逐笔成交
    {
        PSH_StockStepTrade data = reinterpret_cast<PSH_StockStepTrade>(pDataContent);
        printf("shanghai ZC, time=[%d],date=[%d]\n", data->nTradeTime, data->nActionDay);
        break;
    }
    case 1004:// 上交所 level2
    {
        PSH_StockMarketDataL2 data = reinterpret_cast<PSH_StockMarketDataL2>(pDataContent);
		string l2(code);
		l2 += ".csv";
		l2 = str + l2;
		WriteMarketDataL2ToFile(*data, l2);
        printf("shanghai L2, time=[%d],date=[%d]\n", data->nTime, data->nActionDay);
        break;
    }
    case 2001: // 深交所逐笔成交
    {
        PSZ_StockStepTrade data = reinterpret_cast<PSZ_StockStepTrade>(pDataContent);
        printf("shenzhen ZC, time=[%lld],date=[%d]\n", data->i64TransactTime, data->nActionDay);
        break;
    }
    case 2003: // 深交所 委托
    {
        PSZ_StockStepOrder data = reinterpret_cast<PSZ_StockStepOrder>(pDataContent);
        printf("shenzhen ZW, time=[%lld],date=[%d]\n", data->i64TransactTime, data->nActionDay);
        break;
    }
    case 2004://深交所 level2
    {
        PSZ_StockMarketDataL2 data = reinterpret_cast<PSZ_StockMarketDataL2>(pDataContent);
		string l2(code);
		l2 += ".csv";
		l2 = str + l2;
		WriteSZMarketDataL2ToFile(*data, l2);
        printf("shenzhen level2, time=[%d],date=[%d]\n", data->nTime, data->nActionDay);
        break;
    }
    case 2009: // 深交所 逐笔 + 深交所 委托 
    {
        PSZ_StockStepData pZSdata = reinterpret_cast<PSZ_StockStepData>(pDataContent);
        if (pZSdata->type == 1)//SZ ZC 2001
        {
            PSZ_StockStepTrade data = &pZSdata->data.trans;
			string szcode(code);
			Stock_Transaction_SZ localData = *data;
			auto ptr = make_shared<Stock_Transaction_SZ>(localData);
			// 异步执行
			auto futureObj = std::async(std::launch::async, [szcode, ptr]() {
				// instead of a pointer, if necessary.
				StoreDataSZZC(szcode, ptr);
			});
        }
        else if (pZSdata->type == 2)//SZ ZW 2003
        {
            PSZ_StockStepOrder data = &pZSdata->data.order;
			string szcode(code);
			Stock_StepOrder_SZ localData = *data;
			auto ptr = make_shared<Stock_StepOrder_SZ>(localData);
			// 异步执行
			auto futureObj = std::async(std::launch::async, [szcode, ptr]() {
				// instead of a pointer, if necessary.
				StoreDataSZZW(szcode, ptr);
			});
            //printf("SZ ZW 2009,type=2,time=[%lld],date=[%d]\n", data->i64TransactTime, data->nActionDay);
        }
        break;
    }
    // 上海股票基础信息
    case 1008:
    {
        PSH_BASEINFO data = reinterpret_cast<PSH_BASEINFO>(pDataContent);
        printf("code=[%s],upAbsolute=[%lld],downAbsolute=[%lld]\n", code,
            data->nLimitUpAbsolute, data->nLimitDownAbsolute);

        break;
    }
	case 1009:
	{
		//printf("SZ ZC ZW\n");
		printf("code=[%s]\n", code);
		PStockDataSH pZSdata= reinterpret_cast<PStockDataSH>(pDataContent);
		string shcode(code);
		StockDataSH localData;
		localData.nActionDay = pZSdata->nActionDay;
		localData.iBizIndex = pZSdata->iBizIndex;
		localData.nChannel = pZSdata->nChannel;
		localData.nTickTime = pZSdata->nTickTime;
		localData.cType = pZSdata->cType;
		localData.iBuyOrderNo = pZSdata->iBuyOrderNo;
		localData.iSellOrderNo = pZSdata->iSellOrderNo;
		localData.nPrice = pZSdata->nPrice;
		localData.iQty = pZSdata->iQty;
		localData.iTradeMoney = pZSdata->iTradeMoney;
		strcpy(localData.sTickBSflag, pZSdata->sTickBSflag);
		auto ptr = make_shared<StockDataSH>(localData);
		// 异步执行
		auto futureObj = std::async(std::launch::async, [shcode, ptr]() {
			// instead of a pointer, if necessary.
			StoreDataSH(shcode, ptr);
		});
		//shcode += ".csv";
		//shcode = str + shcode;
		//WriteStockDataSHToFile(*pZSdata, shcode, code);
		break;
	}
    // 深圳股票基础信息
    case 2008:
    {
        PSZ_BASEINFO data = reinterpret_cast<PSZ_BASEINFO>(pDataContent);
        int64_t upPrice = 0;   //放大10000倍
        int64_t downPrice = 0; //放大10000倍

        if (data->tCashAuctionParmas.cLimitType == '1') //交易所按照幅度给数据
        {
            auto upRate = data->tCashAuctionParmas.nLimitUpRate;
            auto downRate = data->tCashAuctionParmas.nLimitDownRate;
            auto lastClose = data->tSecurities.i64PrevClosePx;   //昨收价
            printf("code=[%s],lastClose=[%lld],upRate=[%lld],downRate=[%lld]\n", code, lastClose, upRate, downRate);
            ////今日涨停价
            //auto upPrice = lastClose + lastClose * upRate / 100;
            ////今日跌停价
            //auto downPrice = lastClose - lastClose * downRate / 100;

        }
        else if (data->tCashAuctionParmas.cLimitType == '2') //交易所给价格绝对值
        {
            printf("code=[%s],upPriceAbsolute=[%lld],downPriceAbsolute=[%lld]\n", code,
                data->tCashAuctionParmas.nLimitUpAbsolute,
                data->tCashAuctionParmas.nLimitDownAbsolute);
        }
        //printf("code=[%s],limitUpRate=[%ld],limitUpAbsolute=[%lld],updownRate=[%ld],updownAbsolute=[%ld]\n", code,data->tCashAuctionParmas.nLimitUpRate,
        //  data->tCashAuctionParmas.nLimitUpAbsolute,
        //  data->tCashAuctionParmas.nAuctionUpDownRate,
        //  data->tCashAuctionParmas.nAuctionUpDownAbsolute);

        break;
    }
    }//end switch
}

int Work()
{
    while (true)
    {
        std::cout << "\n\n注意!后面那串大写的英文是交易命令，供测试使用\n"
            "\n1.以下命令，普通交易直接输入命令，两融交易在前面加一个CREDIT,例如 CREDIT QUERY ACCOUNT 表示查两融股东账号\n"

            "\t(股票，两融)查询股东账号 : QUERY ACCOUNT\n"
            "\t(股票，两融)撤掉订单(用orderId撤单) : CANCEL SH G123456\n"
            "\t(股票，两融)批量撤单(用orderBsn撤单): CANCELBATCH SH 12345\n"
            "\t(股票，两融)查询订单(0,1,2,3分别表示撤单、正常、可撤、全部): QUERY ORDERS SH 1\n"
            "\t(股票，两融)查询持仓 : QUERY POSITION\n"
            "\t(股票，两融)查询当日成交明细 : QUERY DETAIL\n"
            "\t(股票，两融)查询资金 : QUERY BALANCE\n"
            "\t(股票，两融)组合委托:BUYARRAY\n"

            "\n2.以下命令，只支持普通交易\n"

            "\t限价/市价买/卖 上海深圳股票100股 at 3.14元,MKT表示市价： BUY SH 600000 100 3.14、SELL SZ 000002 100 MKT\n"
            "\tETF篮子委托:BASKET SZ CODE QTY biz_type MKT: BASKET SZ 159901 1 B MKT\n"
            "\t申购单市场ETF(50ETF):  SHENGOU SH 510051 900000\n"
            "\t赎回单市场ETF(50ETF):  SHUHUI SH 510051 900000\n"
            "\t申购跨市场ETF(华泰柏瑞沪深300): SHENGOUKSC SH 510310 900000\n"
            "\t赎回跨市场ETF(华泰柏瑞沪深300): SHUHUIKSC SH 510310 900000\n"
            "\t根据文件下单: TRADEFILE ./file.txt\n"
            "\t国债逆回购: NHG SH 204001 1000 1.5\n"

            "\n3.以下命令只支持 两融交易\n"

            "\t融资融券(融资买入,融券卖出，买券还券，卖券还款,担保品买入,担保品卖出,融资平仓,融券平仓分别使用RZBUY、RQSELL、MQHQ、MQHK、COVERBUY、COVERSELL,RZCLOSE,RQCLOSE):\n"
            "\t\t命令如下:   CREDIT RZBUY SH 601088 100 18.41\n"
            "\t融资融券，负债查询: CREDIT QUERY DEBT\n"
            "\t融资融券，直接还款(0,1分别对应偿还融资欠款、偿还融资费用): CREDIT REPAY 0\n"
            "\t融资融券，合约查询(RZ,RQ分别表示查融资，查融券): CREDIT QUERY CONTRACTS RZ\n"
            "\t融资融券，合约汇总查询: CREDIT QUERY SUMMARY SH\n"
            "\t融资融券，标的券信息查询(FI,SL分别表示当日允许融资、允许融券): CREDIT QUERY TARGET SH FI SL\n"

            "\n4.以下命令只支持股票期权即etf期权,本版本不支持\n"

            "\t期权交易(买开，买平，卖开，卖平，锁券，解锁，备兑开仓，备兑平仓，认购行权，认沽行权分别对应\n"
            "\tBUYOPEN,BUYCLOSE,SELLOPEN,SELLCLOSE,LOCK,UNLOCK,COVEROPEN,COVERCLOSE,CALLEXERCISE,PUTEXERCISE)\n"
            "\t命令如下: OPTION BUYOPEN SH 123456 1 9.9\n"
            "\t期权基础信息查询:OPTION QUERY INFO\n"
            "\t期权组合策略文件查询:OPTION QUERY STRATEGY\n"
            "\t期权可锁定股份查询:OPTION QUERY LOCKABLE SH\n"
            "\t 退出 : q \n" << std::endl;

        std::cout << "please input :";
        std::string input;
        getline(std::cin, input);
        std::cout << "your input is: #" << input << "#" << std::endl;
        if (input == "q")
            return 0;

        int isOption = 0;
        int isCredit = 0;
        if (std::regex_match(input, std::regex(R"(\s*OPTION.*)")))
        {
            // 期权交易，本版本还不支持
            isOption = 1;
        }
        else if (std::regex_match(input, std::regex(R"(\s*CREDIT.*)")))
        {
            //信用交易即两融
            isCredit = 1;
        }
        std::smatch result;
        if (std::regex_match(input, result,
            std::regex(R"(^\w*\s*(BUY|SELL|SHENGOU|SHUHUI|SHENGOUKSC|SHUHUIKSC|NHG|RZBUY|RQSELL|COVERBUY|COVERSELL|MQHQ|MQHK|RZCLOSE|RQCLOSE|XQHQ|BUYOPEN|BUYCLOSE|SELLOPEN|SELLCLOSE|LOCK|UNLOCK|COVEROPEN|COVERCLOSE|CALLEXERCISE|PUTEXERCISE)\s+(SH|SZ)\s+([\d\w]+)\s+(\d+)\s+(MKT|[\.\d]+).*$)")))
        {
            auto cmd = result[1].str();
            auto mkt = result[2].str();
            auto code = result[3].str();
            auto qty = result[4].str();
            auto price = result[5].str();
            printf("match [%s],[%s] [%s] [%s] [%s] [%s] [%s] \n", isOption ? "OPTION" : (isCredit ? "CREDIT" : "NORMAL"),
                result[0].str().c_str(), cmd.c_str(), mkt.c_str(), code.c_str(), qty.c_str(), price.c_str());

            COXReqOrderTicketField req;
            memset(&req, 0, sizeof(COXReqOrderTicketField));

            // 如果 是 普通交易指令，并且配置了 普通资金账户
            if (!isCredit && !isOption && !g_acct.empty() )
            {
                req.AcctType = OX_ACCOUNT_STOCK;

                static std::map<std::string, int> stkBizMap
                {
                    {"BUY",100}, //普通买入
                    {"SELL",101},//普通卖出
                    {"SHENGOU",181},//申购单市场ETF
                    {"SHUHUI",182}, //赎回单市场ETF
                    {"SHENGOUKSC",827}, //申购跨市场ETF
                    {"SHUHUIKSC",828}, //赎回跨市场ETF
                    {"NHG",165} //逆回购
                };

                auto it = stkBizMap.find(cmd);
                if (it != stkBizMap.end())
                {
                    req.StkBiz = it->second;
                }
                else
                {
                    printf("error,unknown cmd=[%s]\n", cmd.c_str());
                    continue;
                }

                snprintf(req.Account, sizeof(req.Account), g_acct.c_str());
                snprintf(req.BoardId, sizeof(req.BoardId), mkt == "SH" ? "10" : "00");
                
                // 注意下单时候 req.Trdacct 是相应板块即boardid的 股东账号，要用OnReqTradeAccount查询出来
                // 这里我在OnRspTradeAccounts 里面已经 给 g_shTradeAcct 填值了，所以可以直接用。
                snprintf(req.Trdacct, sizeof(req.Trdacct), mkt == "SH" ? g_shTrdAcct.c_str() : g_szTrdAcct.c_str());

                snprintf(req.Symbol, sizeof(req.Symbol), code.c_str());

                req.OrderQty = std::atoi(qty.c_str());

                if (price == "MKT") // 市价单
                {
                    // 还有其他种市价单，不过市价单一般不怎么用
                    req.StkBizAction = 121; //最优成交剩撤，上海支持120,121；深圳支持122-125
                    snprintf(req.OrderPrice, sizeof(req.OrderPrice), "0");
                }
                else
                {
                    // 几乎都用限价单
                    req.StkBizAction = 100; //限价单
                    snprintf(req.OrderPrice, sizeof(req.OrderPrice), price.c_str());
                }
                // 可以用 OrderBsn 来标记一笔委托，在 委托应答里面，根据该字段可以找到原委托
                req.OrderBsn = 5555;
                g_TradeApi->OnReqOrderTicket(0, &req);
                std::cout << "place order finished" << std::endl;
            }

            // 如果 是 两融交易
            if (isCredit && !g_acctCredit.empty())
            {
                req.AcctType = OX_ACCOUNT_STOCK;

                static std::map<std::string, int> stkBizMap2 
                {
                    {"RZBUY",702}, // 融资买入
                    {"RQSELL",703},  // 融券卖出
                    {"COVERBUY",700}, //担保品买入
                    {"COVERSELL",701}, //担保品卖出
                    {"MQHQ",704}, // 买券还券
                    {"MQHK",705}, // 卖券还款
                    {"RZCLOSE",706},// 融资平仓
                    {"RQCLOSE",707},// 融券平仓
                    {"XQHQ",710}  // 现券还券
                };

                auto it = stkBizMap2.find(cmd);
                if (it != stkBizMap2.end())
                {
                    req.StkBiz = it->second;
                }
                else
                {
                    printf("error,unknown cmd=[%s]\n",cmd.c_str());
                    continue;
                }

                snprintf(req.Account, sizeof(req.Account), g_acctCredit.c_str());
                snprintf(req.BoardId, sizeof(req.BoardId), (mkt == "SH") ? "10" : "00"); //板块id,上海股票"10"，深圳股票"00"
                snprintf(req.Trdacct, sizeof(req.Trdacct), (mkt == "SH") ? g_shTrdAcctCredit.c_str() : g_szTrdAcctCredit.c_str());

                snprintf(req.Symbol, sizeof(req.Symbol), code.c_str());

                req.OrderQty = std::atoi(qty.c_str());

                if (price == "MKT") // 市价单
                {
                    req.StkBizAction = 121; // 最优成交剩撤
                    snprintf(req.OrderPrice, sizeof(req.OrderPrice), "0");
                }
                else
                {
                    req.StkBizAction = 100; //限价单
                    snprintf(req.OrderPrice, sizeof(req.OrderPrice), price.c_str());
                }
                // 可以用 OrderBsn 来标记一笔委托，在 委托应答里面，根据该字段可以找到原委托,也可用来批量撤单
                req.OrderBsn = 6666;
                g_TradeApi->OnReqOrderTicket(0, &req);
                std::cout << "place order finished" << std::endl;
            }
        }
        else if (regex_match(input, result, std::regex(R"(.*QUERY\s+BALANCE.*)")))
        {
            // 查可用资金(股票，两融都支持)
            COXReqBalanceField req;
            memset(&req, 0, sizeof(COXReqBalanceField));
            req.AcctType = OX_ACCOUNT_STOCK;

            if(isCredit)
            {
                snprintf(req.Account, sizeof(req.Account), g_acctCredit.c_str());
            }
            else
            {
                snprintf(req.Account, sizeof(req.Account), g_acct.c_str());
            }
            g_TradeApi->OnReqQueryBalance(0, &req);
        }
        else if (regex_match(input, result, std::regex(R"(.*QUERY\s+ACCOUNT.*)")))
        {
            //查股东账号(股票，两融都支持)
            //注意看OnRspTradeAccounts ，我给 g_TradeAcct 赋值了，所以才能直接下单
            COXReqTradeAcctField req;
            memset(&req, 0, sizeof(req));
            req.AcctType =  OX_ACCOUNT_STOCK;
            if (isCredit)
            {
                snprintf(req.Account, sizeof(req.Account), g_acctCredit.c_str());
            }
            else
            {
                snprintf(req.Account, sizeof(req.Account), g_acct.c_str());
            }
            g_TradeApi->OnReqTradeAccounts(0, &req);
        }
        else if (regex_match(input, result, std::regex(R"(.*QUERY\s+POSITION.*)")))
        {
            //查持仓(股票，两融都支持)
            COXReqPositionField req;
            memset(&req, 0, sizeof(req));
            req.AcctType = OX_ACCOUNT_STOCK;
            if (isCredit)
            {
                snprintf(req.Account, sizeof(req.Account), g_acctCredit.c_str());
            }
            else
            {
                snprintf(req.Account, sizeof(req.Account), g_acct.c_str());
            }
            req.Flag = '1';
            g_TradeApi->OnReqQueryPositions(0, &req);
        }
        else if (regex_match(input, result, std::regex(R"(.*QUERY\s+ORDERS\s+(SH|SZ)\s+(0|1|2|3).*)")))
        {
            auto mkt = result[1].str();
            auto flag = result[2].str();

            //查询订单(支持股票,两融交易)
            COXReqOrdersField req;
            memset(&req, 0, sizeof(req));
            req.AcctType = OX_ACCOUNT_STOCK;
            if (isCredit)
            {
                snprintf(req.Account, sizeof(req.Account), g_acctCredit.c_str());
            }
            else
            {
                snprintf(req.Account, sizeof(req.Account), g_acct.c_str());
            }

            snprintf(req.BoardId, sizeof(req.BoardId), mkt == "SH" ? "10" : "00");

            req.Flag = flag[0];
            g_TradeApi->OnReqQueryOrders(0, &req);
        }
        else if (regex_match(input, result, std::regex(R"(.*QUERY\s+DETAIL.*)")))
        {
            // 查询当日成交明细(股票，两融都支持)
            COXReqFilledDetailField req;
            memset(&req, 0, sizeof(req));
            req.AcctType = OX_ACCOUNT_STOCK;
            if (isCredit)
            {
                snprintf(req.Account, sizeof(req.Account), g_acctCredit.c_str());
            }
            else
            {
                snprintf(req.Account, sizeof(req.Account), g_acct.c_str());
            }

            req.Flag = '2';// 查全部
            g_TradeApi->OnReqQueryFilledDetails(0, &req);
        }
        else if (regex_match(input, result, std::regex(R"(.*CANCEL\s+(SH|SZ)\s+(\S+).*)")))
        {
            //撤单(股票，两融都支持)
            auto mkt = result[1].str();
            auto code = result[2].str();

            COXReqCancelTicketField req;
            memset(&req, 0, sizeof(req));
            req.AcctType = (isOption ? OX_ACCOUNT_OPTION : OX_ACCOUNT_STOCK);
            snprintf(req.Account, sizeof(req.Account), g_acct.c_str());

            if (mkt == "SH")
            {
                snprintf(req.BoardId, sizeof(req.BoardId), isOption ? "15" : "10");
            }
            else if (mkt == "SZ")
            {
                snprintf(req.BoardId, sizeof(req.BoardId), isOption ? "05" : "00");
            }

            if (isCredit)
            {
                snprintf(req.Account, sizeof(req.Account), g_acctCredit.c_str());
            }
            else
            {
                snprintf(req.Account, sizeof(req.Account), g_acct.c_str());
            }

            snprintf(req.OrderId, sizeof(req.OrderId), code.c_str());
            g_TradeApi->OnReqCancelTicket(0, &req);
        }
        else if (regex_match(input, std::regex(R"(.*CREDIT\s+QUERY\s+DEBT.*)")))
        {
            //融资融券 查询负债(仅支持融资融券)
            COXReqCreditBalanceDebt req;
            memset(&req, 0, sizeof(COXReqCreditBalanceDebt));
            req.AcctType = OX_ACCOUNT_STOCK;
            snprintf(req.Account, sizeof(req.Account), g_acctCredit.c_str());
            req.Currency = '0';
            g_TradeApi->OnReqCreditBalanceDebt(0, &req);
        }
        else if (regex_match(input, result, std::regex(R"(.*CREDIT\s+QUERY\s+CONTRACTS\s+(RZ|RQ).*)")))
        {
            auto contractType = result[1].str();

            // 融资融券合约查询(仅支持融资融券)
            COXReqCreditContracts req;
            memset(&req, 0, sizeof(req));
            req.AcctType = OX_ACCOUNT_STOCK;
            snprintf(req.Account, sizeof(req.Account), g_acctCredit.c_str());
            req.ContractType = (contractType == "RZ" ? '0' : '1');
            g_TradeApi->OnReqCreditContracts(0, &req);
        }
        else if (regex_match(input, result, std::regex(R"(.*CREDIT\s+REPAY\s+(\S+).*)")))
        {
            // 融资融券直接还款
            auto money = result[1].str();
            COXReqCreditRepay req;
            memset(&req, 0, sizeof(COXReqCreditRepay));
            req.AcctType = OX_ACCOUNT_STOCK;
            snprintf(req.Account, sizeof(req.Account), g_acctCredit.c_str());
            snprintf(req.RepayContractAmt, sizeof(req.RepayContractAmt), "%s", money.c_str());
            req.RepayType = '0'; // '0' 偿还融资欠款; '1'偿还融资费用
            g_TradeApi->OnReqCreditRepay(0, &req);
        }
        else if (regex_match(input, result, std::regex(R"(.*CREDIT\s+QUERY\s+SUMMARY\s+(SH|SZ).*)")))
        {
            auto mkt = result[1].str();

            // 融资融券合约汇总查询
            COXReqCreditSLContractSummary req;
            memset(&req, 0, sizeof(req));
            req.AcctType = OX_ACCOUNT_STOCK;
            snprintf(req.Account, sizeof(req.Account), g_acctCredit.c_str());
            snprintf(req.BoardId, sizeof(req.BoardId), mkt == "SH" ? "10" : "00");
            g_TradeApi->OnReqCreditSLContractSummary(111, &req);
        }
        else if (std::regex_match(input, result, std::regex(R"(.*CREDIT\s+QUERY\s+TARGET\s+(SH|SZ)\s+(FI|NO)\s+(SL|NO).*)")))
        {
            auto mkt = result[1].str();
            auto fi = result[2].str();
            auto sl = result[3].str();

            //融资融券标的券信息查询
            COXReqCreditTargetStocks req;
            memset(&req, 0, sizeof(req));
            req.AcctType = OX_ACCOUNT_STOCK;
            snprintf(req.Account, sizeof(req.Account), g_acctCredit.c_str());
            snprintf(req.BoardId, sizeof(req.BoardId), mkt == "SH" ? "10" : "00");
            req.ExchangeId = (mkt == "SH" ? '1' : '0'); // 必需字段
            req.CurrEnableFI = (fi == "FI" ? '1' : '0');
            req.CurrEnableSL = (sl == "SL" ? '1' : '0');
            g_TradeApi->OnReqCreditTargetStocks(111, &req);
        }
        else if (regex_match(input, result, std::regex(R"(.*BASKET\s+(SH|SZ)\s+(\d+)\s+(\d+)\s+(B|P|S|4|5)\s+(MKT|\S+).*)")))
        {
            // etf篮子交易
            auto mkt = result[1].str();
            auto code = result[2].str();
            auto qty = result[3].str();
            auto bizType = result[4].str();
            auto priceInfo = result[5].str();

            COXReqBasketETFOrderTicketField req;
            memset(&req, 0, sizeof(COXReqBasketETFOrderTicketField));
            req.AcctType = OX_ACCOUNT_STOCK;
            snprintf(req.Account, sizeof(req.Account), g_acct.c_str());

            snprintf(req.Trdacct, sizeof(req.Trdacct), mkt == "SH" ? g_shTrdAcct.c_str() : g_szTrdAcct.c_str());
            snprintf(req.BoardId, sizeof(req.BoardId), mkt == "SH" ? "10" : "00");

            //买篮、补篮填写100, 卖篮、留篮、清篮填写101
            req.StkBiz = ((bizType == "B" || bizType == "P") ? 100 : 101);
            req.StkBizAction = 100;
            snprintf(req.Symbol, sizeof(req.Symbol), code.c_str());
            req.OrderQty = std::atoi(qty.c_str());
            if (priceInfo != "MKT")
                snprintf(req.PriceInfo, sizeof(req.PriceInfo), priceInfo.c_str());
            else
                req.StkBizAction = 121;

            //B:买篮,P:补篮,S : 卖篮,4 : 留篮,5 : 清篮
            req.BizType = bizType[0];
            req.AcctSN = 12345;

            g_TradeApi->OnReqETFBasketOrderTicket(0, &req);
        }
        else if (regex_match(input, result, std::regex("^BUYARRAY$")))
        {
            // 下组合委托单(支持股票和两融)
            COXSingleOrderReqInfo orderArray[2];
            memset(orderArray, 0, sizeof(COXSingleOrderReqInfo) * 2);

            auto& req1 = orderArray[0];
            memset(&req1, 0, sizeof(req1));
            snprintf(req1.BoardId, sizeof(req1.BoardId), "00");
            snprintf(req1.Trdacct, sizeof(req1.Trdacct), g_szTrdAcct.c_str());
            snprintf(req1.Symbol, sizeof(req1.Symbol), "000001");
            req1.OrderQty = 100;
            snprintf(req1.OrderPrice, sizeof(req1.OrderPrice), "9.55");

            auto& req2 = orderArray[1];
            memset(&req2, 0, sizeof(req2));
            snprintf(req2.BoardId, sizeof(req2.BoardId), "00");
            snprintf(req2.Trdacct, sizeof(req2.Trdacct), g_szTrdAcct.c_str());
            snprintf(req2.Symbol, sizeof(req2.Symbol), "000002");
            req2.OrderQty = 100;
            snprintf(req2.OrderPrice, sizeof(req2.OrderPrice), "27.5");

            COXReqCompositeOrderTicketField req;
            memset(&req, 0, sizeof(req));
            req.AcctType = OX_ACCOUNT_STOCK;
            snprintf(req.Account, sizeof(req.Account), "%s", g_acct.c_str());
            //req.StkBiz = 700; //担保品买入
            //req.StkBiz = 702; //融资买入
            req.StkBiz = 100; //普通买入
            req.StkBizAction = 100;
            req.TotalCount = 2;
            req.OrdersInfo = orderArray;
            req.ErrorFlag = '1'; // '0',一条失败，整个失败；'1',一条失败，继续下一条
            req.OrderBsn = (int32_t)time(nullptr);

            g_TradeApi->OnReqCompositeOrderTicket(0, &req);
        }
        else if (regex_match(input, result, std::regex(R"(.*CANCELBATCH\s+(SH|SZ)\s+(\d+).*)")))
        {
            //批量撤单(支持股票和两融)
            auto mkt = result[1].str();
            auto bsn = result[2].str();

            COXReqBatchCancelTicketField req;
            memset(&req, 0, sizeof(req));
            snprintf(req.Account, sizeof(req.Account), g_acct.c_str());
            req.AcctType = OX_ACCOUNT_STOCK;

            snprintf(req.BoardId, sizeof(req.BoardId), mkt == "SH" ? "10" : "00");

            req.OrderBsn = std::atoi(bsn.c_str());

            g_TradeApi->OnReqBatchCancelTicket(0, &req);
        }
        else if (std::regex_match(input, result, std::regex(R"(.*QUERYETF\s+(SH|SZ)\s+(\d+)\s+(1|2|3|4|5|6|7|8)\s+(0|1|2).*)")))
        {
            // 同时查询ETF信息和ETF成分股票信息
            auto mkt = result[1].str();
            auto code = result[2].str();
            auto etfType = result[3].str();
            auto etfMode = result[4].str();
            printf("mkt=[%s],etfType=[%s],etfMode=[%s],\n", mkt.c_str(), etfMode.c_str(), etfType.c_str());

            COXReqETFInfoField req;
            memset(&req, 0, sizeof(req));
            req.AcctType = OX_ACCOUNT_STOCK;

            snprintf(req.Account, sizeof(req.Account), "%s", g_acct.c_str());
            snprintf(req.BoardId, sizeof(req.BoardId), (mkt == "SH") ? "10" : "00");

            snprintf(req.ETFCode, sizeof(req.ETFCode), code.c_str());

            req.ETFType = etfType[0];
            req.ETFMode = etfMode[0];

            g_TradeApi->OnReqQueryETFInfo(111, &req);

            g_TradeApi->OnReqQueryETFBasketInfo(222, &req);
        }
        else if (std::regex_match(input, result, std::regex(R"(.*PCF\s+(\S+).*)")))
        {
            // 根据pcf 文件下单
            auto file = result[1].str();
            printf("file=[%s]\n", file.c_str());

            auto fin = fopen(file.c_str(), "r+");
            fseek(fin, 0, SEEK_SET);
            char buf[128] = { 0 };
            std::vector<std::vector<std::string>> vecFile;
            while (fgets(buf, sizeof(buf), fin))
            {
                printf("#####%s#####\n", strtok(buf, "\n"));
                std::string tmp(buf);
                if (std::regex_match(tmp, result, std::regex("^(\\S+)\\s+(\\S+)\\s+(\\S+).*$")))
                {
                    auto code = result[1].str();
                    auto qty = result[2].str();
                    auto price = result[3].str();

                    printf("code=[%s] price=[%s] qty=[%s]\n", code.c_str(), price.c_str(), qty.c_str());
                    vecFile.push_back({ code,price,qty });
                }
                else
                {
                    printf("don't match [%s]\n", tmp.c_str());
                }
            }
            printf("read symbol = [%lu]\n", vecFile.size());

            for (auto e : vecFile)
            {
                COXReqOrderTicketField req;
                req.AcctType = OX_ACCOUNT_STOCK;
                snprintf(req.Account, sizeof(req.Account), "%s", g_acct.c_str());
                req.StkBiz = 100; // 买
                req.StkBizAction = 121; //市价买剩撤
                snprintf(req.BoardId, sizeof(req.BoardId), "10");
                snprintf(req.Trdacct, sizeof(req.Trdacct), "%s", g_shTrdAcct.c_str());
                snprintf(req.Symbol, sizeof(req.Symbol), e[0].c_str());
                req.OrderQty = atoi(e[2].c_str());
                req.OrderBsn = 111111;
                g_TradeApi->OnReqOrderTicket(0, &req);
                printf("pace order finished\n");
            }
        }
        else
        {
            std::cout << "unsupport command" << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}

void parsetags(StrTag subscribe[],string &tags,int &index) {
	int pos = 0;
	for (int i = 0; i < tags.size(); i++)
	{
		if (tags[i] == '|')
		{
			index++;
			pos = 0;
		}
		else
		{
			subscribe[index][pos] = tags[i];
			pos++;
		}
	}
}

void hangqing(StrTag tag,string &marketDataJsonConfig) {

	// 步骤2：设置 行情回调函数
	// SetDoMsg 有一个默认参数pParam,改参数可用来 传递一个 对象的地址，例如SetDoMsg(MyDoMsg,&obj)
	// 这样在 MyDoMsg 里面，你可以 obj->Func,来调用函数。没有这种需求，直接使用默认NULL即可。
	GTPMarketDataApi *g_marketDataAPI = gtpCreateMarketDataApiV2(marketDataJsonConfig.c_str());
	StrTag subscribe[1] = { 0 };
	strcpy(subscribe[0], tag);
	cout << "subsc" << tag << endl;
	string str(tag);
	cout <<"str"<< str << endl;
	g_marketDataAPI->SetDoMsg(MyDoMsg,tag);
	auto ret = g_marketDataAPI->Connect();
	if (ret != 0)
	{
		printf("real time market data connect failed,ret =[%d]\n", ret);
		g_marketDataAPI->Close();
	}

	// 步骤3: 订阅 行情。 其中 subscribe 是形如 "SH.*.L1","SZ.000002.L2","SH.600000.ZC".... 这样的tag 数组
	// 有哪些tags 可以参考 <<gtp_release.zip>> others目录下<<中畅原生api客户接入文档>>业务介绍文档下面的几份excel表格
	g_marketDataAPI->SetSubscribeTags(subscribe, 1);
	ret = g_marketDataAPI->Subscribe();
	printf("subscribe[0]=[%s],ret=[%d]", (char*)subscribe, ret);
	// 如果单独使用行情，要防止本线程退出，可以加个getchar(),或者 干脆 sleep() 一段时间
	getchar();
}

void createDir() {
	const wchar_t* folderName = L"hangqing_data";

	// 创建文件夹
	if (CreateDirectory(folderName, NULL)) {
		std::wcout << L"Folder created successfully: " << folderName << std::endl;
	}
	else {
		std::wcerr << L"Failed to create folder." << std::endl;
	}
}


int main(int argc, char* argv[])
{
	createDir();
    //INIReader reader("C:\\Users\\tomst\\Desktop\\gtp5.6_release.zip(1)\\gtp5.6_release\\demo\\source\\x64\\Release\\config.ini");
                     	INIReader reader("./config.ini");
    if (reader.ParseError() < 0)
    {
        fprintf(stderr, "can't find ./config.ini ,exit\n");
        return -1;
    }

	std::string marketDataJsonConfig = reader.Get("user", "marketDataJsonConfig", "");
    std::string tradeJsonConfig = reader.Get("user", "tradeJsonConfig", "");
    
    // 非常重要！！！

    // 本demo/main.cpp 是为了方便演示，所以 才创建了 ./config.ini ，
    // 里面包含 user ,tradeJsonConfig,marketDataJsonConfig......这些字段
    // 这些字段  不是  必须的!
    //
    // 客户在实际使用时候，只需要满足以下2条：
    // 1.交易接口 gtpCreateTradeApiV2(const char* jsonConfig1), 入参jsonConfig1 按照 格
    //
    //"{ \"custom_code\":\"110000035017\",\"run_mode\" : \"0\",\"gtp_run_addr_internet\" : \"61.142.2.101:29003\",\"gtp_run_addr_production\" : \"10.35.86.126:29003\",\"log_prop_path\" : \"./Tradelog.prop\" }"
    // 
    // 即可，其中custom_code 填你自己的实盘客户代码，run_mode 填 0 、1 或2，分别表示 测试，仿真，实盘。其他的不用改变，其他的不用改变，其他的不用改变。
    // 程序在启动的时候，会自动从上述 ip地址 下拉 相应的 交易柜台地址，所以不用再问我交易地址
    //
    // 2.行情接口 gtpCreateMarketDataApiV2(const char* jsonConfig2), 入参 jsonConfig2 按照 格式
    //
    // "{ \"real_time_account\":\"web_cltg\",\"real_time_password\" : \"web_cltg\",\"real_time_ip1\" : \"61.142.2.100\",\"real_time_port1\":\"9003\"
    // ,\"real_time_ip2\" : \"61.142.2.100\",\"real_time_port2\":\"9003\",\"log_prop_path\" : \"./Tradelog.prop\"}"
    //
    // 即可，real_time_account ，real_time_password 填你自己的 行情账号密码，web_cltg 仅供测试，不允许实盘使用！  
    // 测试模式行情地址 61.142.2.100：9003; 仿真和实盘模式都是真实行情，行情地址 10.35.80.210/211:10052
    // 即 在互联网测试 行情地址 real_time_ip1 ,real_time_ip2  以及port 都填 61.142.2.100; 国信内网仿真和实盘 填 10.35.80.210/211:10052

    // 注意，注意：强烈建议 客户把 配置参数 ，即上述jsonConfig 放进配置文件里面，不要在代码里面写死！ 以防有时候需要改参数

    // 行情使用方法
    if (!marketDataJsonConfig.empty())
    {
		// 步骤1: 创建一个行情对象
		// demo 中为了方便写代码，最多50个tag，实际上可以几百个，建议可以直接SH.*.L1这样1个tag表示全市场所有
		// 然后从行情推送里面 过滤 出你想要的 行情数据
		// demo 中为了方便写代码，最多50个tag，实际上可以几百个，建议可以直接SH.*.L1这样1个tag表示全市场所有
		// 然后从行情推送里面 过滤 出你想要的 行情数据
		StrTag subscribe[50] = { 0 };
		int index = 0;
		auto tags = reader.Get("user", "subscribeTags", "");
		parsetags(subscribe, tags, index);
		printf("haha");
		vector<std::thread> threads(50);
		for (int i = 0; i < index; i++) {
			threads[i]= std::thread(hangqing, subscribe[i],marketDataJsonConfig);
		}

		for (int i = 0; i < index; i++) {
			if (threads[i].joinable()) {
				threads[i].join();
			}
		}
    }

    // 交易使用方法
    if (!tradeJsonConfig.empty())
    {
        printf("tradeJsonConfig=[%s]\n", tradeJsonConfig.c_str());

        // 步骤1：用客户代码，ip地址，创建一个交易对象
        // 该函数是非阻塞的，如果配置错误，会失败，里面会exit自动退出程序
        g_TradeApi = gtpCreateTradeApiV2(tradeJsonConfig.c_str());

        // 步骤2：设置回调函数
        // 注意该spi 不要定义到栈里，否则生命期太短，回调时候可能找不到
        // 该函数是非阻塞的，一定成功
        auto pStkSpi = new StkSpi;
        g_TradeApi->RegisterSpi(pStkSpi);

        // 步骤3：启动交易连接
        // 连接成功之后 ，会触发 OnConnected函数
        // 该函数是阻塞的，有可能失败，卡住，或者直接返回非0值，此时请检查LOG/trade_api.*.log
        // 如果成功后，会回调 OnConnected函数
        auto ret = g_TradeApi->Start();
        printf("trade api start ,ret=[%d]\n", ret);

        while (!g_Connected)
        {
            printf("sleep , wait connect\n");
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        // 步骤4： 登录资金账号

        // 所有OnReq 请求函数发消息都是 非阻塞的， 会通过异步回调OnRsp 把结果返回

        // demo为了演示，同时登录 普通资金账号 和 信用资金账号，可以只登录你要用的那个即可
        // acct ,acctPassword.... 这些字段，都是我demo里面为了方便设置的，不是 配置文件里面必须的
        // 客户可以用自己的配置文件，或者 直接在 代码里面写死

        // 普通资金账号登录
        {
            g_acct = reader.Get("user", "account", "");
            g_passwd = reader.Get("user", "password", "");
            // 创建的struct 结构体，建议都用memset 清零下，已有多位客户，出现struct 中未清零
            // 送过来的struct 里面数据 乱了 的问题 
            // 后面所有 struct 入参 也请都清零下，不再赘叙
            COXReqLogonField req;
            memset(&req, sizeof(req), 0);
            snprintf(req.Account, sizeof(req.Account), g_acct.c_str());
            req.AcctType = OX_ACCOUNT_STOCK;
            snprintf(req.Password, sizeof(req.Password), g_passwd.c_str());
            g_TradeApi->OnReqLogon(0, &req);
        }

        // 信用资金账号登录(即融资融券)
        {
            g_acctCredit = reader.Get("user", "accountCredit", "");
            g_passwd = reader.Get("user", "password", "");
            COXReqLogonField req;
            memset(&req, sizeof(req), 0);
            snprintf(req.Account, sizeof(req.Account), g_acctCredit.c_str());
            req.AcctType = OX_ACCOUNT_STOCK;
            snprintf(req.Password, sizeof(req.Password), g_passwd.c_str());
            g_TradeApi->OnReqLogon(0, &req);
        }

        // 等上述资金账号 全部登录成功
        while (g_LogOn != 2)
        {
            printf("sleep , wait Logon\n");
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        // demo 为了方便演示，做了一个 命令行对话框，客户可以参照里面的 命令，把要用的 交易接口代码，抠到自己的程序里面去
        // 可能会乱码，设置 终端的 字符编码即可，gbk2312
        Work();

    }

    if (g_marketDataAPI)
    {
        g_marketDataAPI->Close();
    }

    if (g_TradeApi)
    {
        g_TradeApi->Stop();
    }
    getchar();
    return 0;
}

