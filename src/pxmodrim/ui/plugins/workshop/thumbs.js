.pragma library

// Steam's image CDN resizes on request. Asking for the size that is shown keeps
// both the download and the decoded pixmap small.
function sized(url, width, height) {
    if (!/^https:\/\/(?:[a-z0-9-]+\.)*(?:steamusercontent\.com|steamstatic\.com)\//i.test(url))
        return url

    const fragmentAt = url.indexOf("#")
    const fragment = fragmentAt < 0 ? "" : url.slice(fragmentAt)
    const address = fragmentAt < 0 ? url : url.slice(0, fragmentAt)
    const queryAt = address.indexOf("?")
    const path = queryAt < 0 ? address : address.slice(0, queryAt)
    const query = queryAt < 0 ? [] : address.slice(queryAt + 1).split("&").filter(function (part) {
        return part !== "" && !/^(imw|imh|ima|impolicy)=/.test(part)
    })
    query.push("imw=" + width, "imh=" + height, "ima=fit", "impolicy=Letterbox")
    return path + "?" + query.join("&") + fragment
}

// Round up to a step so that small layout changes do not reload the image.
function bucket(value, step) {
    return Math.max(step, Math.ceil(value / step) * step)
}
