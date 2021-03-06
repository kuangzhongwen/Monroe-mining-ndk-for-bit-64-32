/* XMRig
 * Copyright 2010      Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2012-2014 pooler      <pooler@litecoinpool.org>
 * Copyright 2014      Lucas Jones <https://github.com/lucasjones>
 * Copyright 2014-2016 Wolf9466    <https://github.com/OhGodAPet>
 * Copyright 2016      Jay D Dee   <jayddee246@gmail.com>
 * Copyright 2017-2018 XMR-Stak    <https://github.com/fireice-uk>, <https://github.com/psychocrypt>
 * Copyright 2016-2018 XMRig       <https://github.com/xmrig>, <support@xmrig.com>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef _MSC_VER
#pragma warning(disable:4244)
#endif

#include <inttypes.h>
#include <memory>
#include <time.h>

#include "App.h"
#include "api/Api.h"
#include "common/log/Log.h"
#include "common/net/Client.h"
#include "common/net/strategies/FailoverStrategy.h"
#include "common/net/strategies/SinglePoolStrategy.h"
#include "common/net/SubmitResult.h"
#include "core/Config.h"
#include "core/Controller.h"
#include "net/Network.h"
#include "net/strategies/DonateStrategy.h"
#include "workers/Workers.h"
#include "common/log/AndroidLog.h"
#include "StringUtils.h"

Network::Network(xmrig::Controller *controller) :
    m_donate(nullptr),
    m_controller(controller)
{
    srand(time(0) ^ (uintptr_t) this);

    Workers::setListener(this);

    const std::vector<Pool> &pools = controller->config()->pools();

    if (pools.size() > 1) {
        m_strategy = new FailoverStrategy(pools, controller->config()->retryPause(), controller->config()->retries(), this);
    }
    else {
        m_strategy = new SinglePoolStrategy(pools.front(), controller->config()->retryPause(), controller->config()->retries(), this);
    }

    if (controller->config()->donateLevel() > 0) {
        m_donate = new DonateStrategy(controller->config()->donateLevel(), controller->config()->pools().front().user(), controller->config()->algorithm().algo(), this);
    }

    m_timer.data = this;
    uv_timer_init(uv_default_loop(), &m_timer);

    uv_timer_start(&m_timer, Network::onTick, kTickInterval, kTickInterval);
}


Network::~Network()
{
}


void Network::connect()
{
    m_strategy->connect();
}


void Network::stop()
{
    if (m_donate) {
        m_donate->stop();
    }

    m_strategy->stop();

    onPoolDisconnect("stop");
}


void Network::onActive(IStrategy *strategy, Client *client)
{
    if (m_donate && m_donate == strategy) {
        return;
    }

    m_state.setPool(client->host(), client->port(), client->ip());

    LOGD("use pool %s:%d %s", client->host(), client->port(), client->ip());
    onConnectPoolSuccess();
}


void Network::onJob(IStrategy *strategy, Client *client, const Job &job)
{
    if (m_donate && m_donate->isActive() && m_donate != strategy) {
        return;
    }

    setJob(client, job, m_donate == strategy);
}


void Network::onJobResult(const JobResult &result)
{
    LOGD("%s", "Network/onJobResult");
    if (result.poolId == -1 && m_donate) {
        m_donate->submit(result);
        return;
    }
    m_strategy->submit(result);
}


void Network::onPause(IStrategy *strategy)
{
    if (m_donate && m_donate == strategy) {
        m_strategy->resume();
    }

    if (!m_strategy->isActive()) {
        LOGD("%s", "no active pools, stop mining");
        m_state.stop();
        return Workers::pause();
    }
}


void Network::onResultAccepted(IStrategy *strategy, Client *client, const SubmitResult &result, const char *error)
{
    m_state.add(result, error);

    std::string message;
    if (error) {
        LOGD("rejected (%" PRId64 "/%" PRId64 ") diff %u \"%s\" (%" PRIu64 " ms)",
                              m_state.accepted, m_state.rejected, result.diff, error, result.elapsed);
        message = "rejected (";
    }
    else {
        LOGD("accepted (%" PRId64 "/%" PRId64 ") diff %u (%" PRIu64 " ms)",
                              m_state.accepted, m_state.rejected, result.diff, result.elapsed);
        message = "accepted (";
    }

    message += toString(m_state.accepted);
    message += "/";
    message += toString(m_state.rejected);
    message += ")";
    message += " diff ";
    message += toString(result.diff);
    message += "(";
    message += toString(result.elapsed);
    message += " ms)";
    const char *p = message.c_str();
    onMessageFromPool(p);
}


bool Network::isColors() const
{
    return m_controller->config()->isColors();
}

void Network::setJob(Client *client, const Job &job, bool donate)
{
    LOGD("new job from %s:%d diff %d algo %s",
                      client->host(), client->port(), job.diff(), job.algorithm().shortName());
    std::string message = "new job from";
    message += " ";
    message += client->host();
    message += ":";
    message += toString(client->port());
    message += " diff ";
    message += toString(job.diff());
    message += " algo ";
    message += job.algorithm().shortName();
    const char *p = message.c_str();
    onMessageFromPool(p);

    m_state.diff = job.diff();
    Workers::setJob(job, donate);
}


void Network::tick()
{
    const uint64_t now = uv_now(uv_default_loop());

    m_strategy->tick(now);

    if (m_donate) {
        m_donate->tick(now);
    }

#   ifndef XMRIG_NO_API
    Api::tick(m_state);
#   endif
}


void Network::onTick(uv_timer_t *handle)
{
    static_cast<Network*>(handle->data)->tick();
}
