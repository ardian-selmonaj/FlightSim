/* Copyright Matthew Reid
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "HelpDisplayRenderOperation.h"
#include "SkyboltEngine/VisHud.h"
#include <SkyboltVis/Scene.h>
#include <SkyboltVis/RenderOperation/RenderTarget.h>
#include <SkyboltVis/Window/Window.h>

namespace skybolt {

HelpDisplayRenderOperation::HelpDisplayRenderOperation()
{
	mHud = osg::ref_ptr<VisHud>(new VisHud());
	addChild(mHud);
}

void HelpDisplayRenderOperation::setMessage(const std::string& message)
{
	mMessage = message;
	draw();
}

void HelpDisplayRenderOperation::draw()
{
	const float lineHeight = 0.05f;
	const float textSize = lineHeight * 0.8f;
	const float margin = 0.1f;

	// The HUD spans -aspectRatio to aspectRatio from the left to the right edge
	mHud->clear();
	mHud->drawText(glm::vec2(-mAspectRatio + margin, 1.0f - margin), mMessage, 0.0f, textSize);
}

void HelpDisplayRenderOperation::setVisible(bool visible)
{
	bool currentlyVisible = isVisible();
	if (visible && !currentlyVisible)
	{
		addChild(mHud);
	}
	else if (!visible && currentlyVisible)
	{
		removeChild(mHud);
	}
}

bool HelpDisplayRenderOperation::isVisible()
{
	return (mHud->getNumParents() > 0);
}

void HelpDisplayRenderOperation::updatePreRender(const vis::RenderContext& context)
{
	float aspectRatio = vis::calcAspectRatio(context);
	mHud->setAspectRatio(aspectRatio);
	if (aspectRatio != mAspectRatio)
	{
		mAspectRatio = aspectRatio;
		draw();
	}
}

} // namespace skybolt